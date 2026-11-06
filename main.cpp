#include <windows.h>
#include <stdio.h>
#include <time.h>

#define MIN_FILE_SIZE (100 * 1024) // Не менее 100 Кбайт (102400 байт)
#define MAX_FILE_BUFFER (200 * 1024) // Буфер ОП с запасом (200 Кбайт)
#define REPEAT_COUNT 20            // Повторить операцию 20 раз
#define MIN_LINE_LEN 64            // Минимальный предел характеристики
#define MAX_LINE_LEN 128           // Максимальный предел характеристики

// Вспомогательная функция для вывода ошибки и завершения
void PrintErrorAndExit(const char* stepName) {
    DWORD errCode = GetLastError();
    printf("[ОШИБКА] Сбой на этапе '%s'. Код ошибки Win32: %lu\n", stepName, errCode);
    ExitProcess(1);
}

// Функция генерации тестового файла со строками случайной длины (до maxLineLen)
DWORD GenerateTestFile(const char* filename, int maxLineLen, HANDLE hHeap) {
    HANDLE hFile = CreateFileA(
        filename,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        PrintErrorAndExit("CreateFileA (генерация файла)");
    }

    // Выделяем временный буфер для формирования одной строки через HeapAlloc (без malloc)
    char* lineBuf = (char*)HeapAlloc(hHeap, HEAP_ZERO_MEMORY, maxLineLen + 1);
    if (lineBuf == NULL) {
        CloseHandle(hFile);
        PrintErrorAndExit("HeapAlloc (буфер генерации строки)");
    }

    DWORD totalBytesWritten = 0;

    while (totalBytesWritten < MIN_FILE_SIZE) {
        // Длина строки от 4 до maxLineLen байт (включая \r\n)
        int currentLen = 4 + (rand() % (maxLineLen - 3));
        
        for (int i = 0; i < currentLen - 2; i++) {
            lineBuf[i] = 'A' + (rand() % 26);
        }
        lineBuf[currentLen - 2] = '\r';
        lineBuf[currentLen - 1] = '\n';

        DWORD bytesWritten = 0;
        BOOL writeRes = WriteFile(hFile, lineBuf, currentLen, &bytesWritten, NULL);
        if (!writeRes || bytesWritten != (DWORD)currentLen) {
            HeapFree(hHeap, 0, lineBuf);
            CloseHandle(hFile);
            PrintErrorAndExit("WriteFile (запись тестовых данных)");
        }

        totalBytesWritten += bytesWritten;
    }

    if (!HeapFree(hHeap, 0, lineBuf)) {
        CloseHandle(hFile);
        PrintErrorAndExit("HeapFree (буфер генерации строки)");
    }

    if (!CloseHandle(hFile)) {
        PrintErrorAndExit("CloseHandle (после генерации файла)");
    }

    return totalBytesWritten;
}

int main() {
    // Установка русской локали для консоли Windows (CP1251 / UTF-8)
    SetConsoleOutputCP(1251);
    srand((unsigned int)time(NULL));

    // Получаем дескриптор кучи процесса
    HANDLE hHeap = GetProcessHeap();
    if (hHeap == NULL) {
        PrintErrorAndExit("GetProcessHeap");
    }

    // Выделяем в ОП целевой буфер, куда будет считываться весь файл построчно
    char* ramDestinationBuffer = (char*)HeapAlloc(hHeap, HEAP_ZERO_MEMORY, MAX_FILE_BUFFER);
    if (ramDestinationBuffer == NULL) {
        PrintErrorAndExit("HeapAlloc (основной буфер ОП)");
    }

    // Выделяем буфер для чтения одной строки максимального размера
    char* tempReadBuffer = (char*)HeapAlloc(hHeap, HEAP_ZERO_MEMORY, MAX_LINE_LEN + 1);
    if (tempReadBuffer == NULL) {
        HeapFree(hHeap, 0, ramDestinationBuffer);
        PrintErrorAndExit("HeapAlloc (буфер построчного чтения)");
    }

    // Получаем частоту высокоточного таймера
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency)) {
        HeapFree(hHeap, 0, tempReadBuffer);
        HeapFree(hHeap, 0, ramDestinationBuffer);
        PrintErrorAndExit("QueryPerformanceFrequency");
    }

    // Открываем CSV-файл для сохранения результатов (для построения графика в Excel)
    FILE* csvFile = fopen("results_var5.csv", "w");
    if (csvFile == NULL) {
        HeapFree(hHeap, 0, tempReadBuffer);
        HeapFree(hHeap, 0, ramDestinationBuffer);
        printf("[ОШИБКА] Не удалось создать файл results_var5.csv\n");
        return 1;
    }
    fprintf(csvFile, "MaxLineSize_Bytes;Speed_KBps\n");

    printf("Запуск лабораторной работы №1 (Вариант 5)...\n");
    printf("------------------------------------------------------------\n");
    printf("| Макс. длина строки (байт) | Скорость обмена (Кбайт/с)    |\n");
    printf("------------------------------------------------------------\n");

    const char* testFileName = "test_data_var5.txt";

    // Основной цикл по характеристике: максимальный размер строки от 64 до 128 байт
    for (int maxLine = MIN_LINE_LEN; maxLine <= MAX_LINE_LEN; maxLine++) {
        
        // 1. Генерируем файл размером >= 100 КБ со строками до maxLine байт
        DWORD actualFileSize = GenerateTestFile(testFileName, maxLine, hHeap);

        // 2. Открываем файл для чтения
        HANDLE hFile = CreateFileA(
            testFileName,
            GENERIC_READ,
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (hFile == INVALID_HANDLE_VALUE) {
            fclose(csvFile);
            HeapFree(hHeap, 0, tempReadBuffer);
            HeapFree(hHeap, 0, ramDestinationBuffer);
            PrintErrorAndExit("CreateFileA (открытие для чтения)");
        }

        LARGE_INTEGER startTime, endTime;
        if (!QueryPerformanceCounter(&startTime)) {
            CloseHandle(hFile);
            PrintErrorAndExit("QueryPerformanceCounter (start)");
        }

        // 3. Повторяем операцию построчного чтения файла 20 раз
        for (int iter = 0; iter < REPEAT_COUNT; iter++) {
            // Возвращаем указатель файла в самое начало
            DWORD ptrRes = SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
            if (ptrRes == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) {
                CloseHandle(hFile);
                PrintErrorAndExit("SetFilePointer (в начало файла)");
            }

            DWORD ramOffset = 0;

            // Читаем файл построчно
            while (1) {
                DWORD bytesRead = 0;
                BOOL readRes = ReadFile(hFile, tempReadBuffer, maxLine, &bytesRead, NULL);
                if (!readRes) {
                    CloseHandle(hFile);
                    PrintErrorAndExit("ReadFile (чтение строки)");
                }

                if (bytesRead == 0) {
                    break; // Достигнут конец файла (EOF)
                }

                // Ищем символ конца строки '\n' в считанном блоке
                DWORD lineLength = bytesRead;
                for (DWORD i = 0; i < bytesRead; i++) {
                    if (tempReadBuffer[i] == '\n') {
                        lineLength = i + 1; // Длина вместе с '\n'
                        break;
                    }
                }

                // Копируем найденную строку в оперативную память через CopyMemory
                if (ramOffset + lineLength <= MAX_FILE_BUFFER) {
                    CopyMemory(ramDestinationBuffer + ramOffset, tempReadBuffer, lineLength);
                    ramOffset += lineLength;
                }

                // Если считали больше, чем одна строка, сдвигаем указатель файла назад к началу следующей строки
                if (lineLength < bytesRead) {
                    LONG moveBack = -((LONG)(bytesRead - lineLength));
                    DWORD seekRes = SetFilePointer(hFile, moveBack, NULL, FILE_CURRENT);
                    if (seekRes == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) {
                        CloseHandle(hFile);
                        PrintErrorAndExit("SetFilePointer (выравнивание по строке)");
                    }
                }
            }
        }

        if (!QueryPerformanceCounter(&endTime)) {
            CloseHandle(hFile);
            PrintErrorAndExit("QueryPerformanceCounter (end)");
        }

        if (!CloseHandle(hFile)) {
            PrintErrorAndExit("CloseHandle (после чтения)");
        }

        // 4. Вычисляем время (в секундах) и скорость обмена (Кбайт/с)
        double elapsedSeconds = (double)(endTime.QuadPart - startTime.QuadPart) / (double)frequency.QuadPart;
        double totalKilobytesRead = ((double)actualFileSize * REPEAT_COUNT) / 1024.0;
        double speedKBps = totalKilobytesRead / elapsedSeconds;

        printf("| %-25d | %-28.2f |\n", maxLine, speedKBps);
        fprintf(csvFile, "%d;%.2f\n", maxLine, speedKBps);
    }

    printf("------------------------------------------------------------\n");
    printf("Работа успешно завершена! Данные сохранены в 'results_var5.csv'.\n");

    // Освобождаем ресурсы и проверяем на ошибки
    fclose(csvFile);
    DeleteFileA(testFileName);

    if (!HeapFree(hHeap, 0, tempReadBuffer)) {
        PrintErrorAndExit("HeapFree (tempReadBuffer)");
    }
    if (!HeapFree(hHeap, 0, ramDestinationBuffer)) {
        PrintErrorAndExit("HeapFree (ramDestinationBuffer)");
    }

    return 0;
}