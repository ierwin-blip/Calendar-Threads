#include <array>
#include <cerrno>
#include <iomanip>
#include <iostream>
#include <semaphore.h>
#include <stdexcept>
#include <string>
#include <thread>

constexpr int MONTHS = 12;
constexpr int WIDTH = 21;
constexpr int LINES = 8; // Title, weekday headings, and six weeks

const std::array<std::string, MONTHS> monthNames = {
    "January", "February", "March", "April",
    "May", "June", "July", "August",
    "September", "October", "November", "December"
};

struct MonthInfo {
    int month;
    int startDay;
    int days;
    int year;
    int monthsPerRow;
    sem_t* semaphores;
};

// Reject arguments such as "3abc" instead of accepting just the 3.
int parseInteger(const char* argument) {
    std::string text(argument);
    std::size_t used = 0;

    int value = std::stoi(text, &used);

    if (used != text.size()) {
        throw std::invalid_argument("Invalid integer");
    }

    return value;
}

void waitForBaton(sem_t* semaphore) {
    // A signal can interrupt sem_wait; retry in that case.
    while (sem_wait(semaphore) == -1) {
        if (errno != EINTR) {
            std::terminate();
        }
    }
}

void passBaton(sem_t* semaphore) {
    if (sem_post(semaphore) == -1) {
        std::terminate();
    }
}

void printMonth(MonthInfo info) {
    const int rowStart =
        (info.month / info.monthsPerRow) * info.monthsPerRow;

    int rowEnd = rowStart + info.monthsPerRow - 1;

    if (rowEnd >= MONTHS) {
        rowEnd = MONTHS - 1;
    }

    for (int line = 0; line < LINES; ++line) {
        waitForBaton(&info.semaphores[info.month]);

        if (line == 0) {
            // Center the title within the month column.
            std::string title =
                monthNames[info.month] + " " + std::to_string(info.year);

            int leftPadding =
                (WIDTH - static_cast<int>(title.size())) / 2;

            int rightPadding =
                WIDTH - leftPadding - static_cast<int>(title.size());

            std::cout << std::string(leftPadding, ' ')
                      << title
                      << std::string(rightPadding, ' ');
        } else if (line == 1) {
            std::cout << "  S  M  T  W  R  F  S";
        } else {
            int week = line - 2;

            for (int weekday = 0; weekday < 7; ++weekday) {
                int day = week * 7 + weekday - info.startDay + 1;

                if (day >= 1 && day <= info.days) {
                    std::cout << std::setw(3) << day;
                } else {
                    std::cout << std::setw(3) << "";
                }
            }
        }

        if (info.month < rowEnd) {
            // Continue across the current line.
            std::cout << "  ";
            passBaton(&info.semaphores[info.month + 1]);
        } else {
            // The last month in the row ends the output line.
            std::cout << '\n';

            if (line < LINES - 1) {
                // Return to the first month for the next line.
                passBaton(&info.semaphores[rowStart]);
            } else if (rowEnd + 1 < MONTHS) {
                // This row is finished. Start the next row of months.
                std::cout << '\n';
                passBaton(&info.semaphores[rowEnd + 1]);
            }
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <months-per-row> <January-start-day> <year>\n";
        return 1;
    }

    int monthsPerRow;
    int startDay;
    int year;

    try {
        monthsPerRow = parseInteger(argv[1]);
        startDay = parseInteger(argv[2]);
        year = parseInteger(argv[3]);
    } catch (const std::exception&) {
        std::cerr << "Error: all three arguments must be integers.\n";
        return 1;
    }

    if (monthsPerRow < 1 || monthsPerRow > MONTHS ||
        startDay < 0 || startDay > 6 ||
        year < 1 || year > 9999) {
        std::cerr << "Error: months per row must be 1-12, "
                  << "starting day must be 0-6, "
                  << "and year must be 1-9999.\n";
        return 1;
    }

    // The assignment describes the year as being for display purposes.
    // Therefore, February has 28 days in this version.
    const std::array<int, MONTHS> daysInMonth = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    std::array<sem_t, MONTHS> semaphores;
    std::array<MonthInfo, MONTHS> monthInfo;
    std::array<std::thread, MONTHS> threads;

    // Only January starts with permission to print.
    for (int month = 0; month < MONTHS; ++month) {
        unsigned int initialValue = (month == 0) ? 1 : 0;

        if (sem_init(&semaphores[month], 0, initialValue) == -1) {
            std::cerr << "Error: could not initialize a semaphore.\n";

            for (int previous = 0; previous < month; ++previous) {
                sem_destroy(&semaphores[previous]);
            }

            return 1;
        }
    }

    // Calculate each month's starting weekday.
    int currentStart = startDay;

    for (int month = 0; month < MONTHS; ++month) {
        monthInfo[month] = {
            month,
            currentStart,
            daysInMonth[month],
            year,
            monthsPerRow,
            semaphores.data()
        };

        currentStart = (currentStart + daysInMonth[month]) % 7;
    }

    // Create exactly one thread per month.
    for (int month = 0; month < MONTHS; ++month) {
        threads[month] = std::thread(printMonth, monthInfo[month]);
    }

    // Wait until every month has finished printing.
    for (std::thread& thread : threads) {
        thread.join();
    }

    for (sem_t& semaphore : semaphores) {
        sem_destroy(&semaphore);
    }

    // Arrays clean up automatically; no new/delete is necessary.
    return 0;
}