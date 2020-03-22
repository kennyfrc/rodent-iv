/*
Rodent, a UCI chess playing engine derived from Sungorus 1.4
Copyright (C) 2009-2011 Pablo Vazquez (Sungorus author)
Copyright (C) 2011-2019 Pawel Koziol
Copyright (C) 2020-2020 Bernhard C. Maerz

Rodent is free software: you can redistribute it and/or modify it under the terms of the GNU
General Public License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

Rodent is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along with this program.
If not, see <http://www.gnu.org/licenses/>.
*/

#include <cstdio>
#include <cstring>
#include <string>
#include <stdarg.h>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #include <wchar.h>
#else
    #include <unistd.h>
    #include <sys/time.h>
    #ifndef ANDROID
        #include <wordexp.h>
    #endif
#endif

#include "rodent.h"

int Percent(int val, int perc) {
    return (val * perc) / 100;
}

bool InputAvailable() {

#if defined(_WIN32) || defined(_WIN64)
    static bool init = false, pipe;
    static HANDLE inh;
    DWORD dw;

    if (!init) {
        init = true;
        inh = GetStdHandle(STD_INPUT_HANDLE);
        pipe = !GetConsoleMode(inh, &dw);
        if (!pipe) {
            SetConsoleMode(inh, dw & ~(ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT));
            FlushConsoleInputBuffer(inh);
        }
    }
    if (pipe) {
        if (!PeekNamedPipe(inh, NULL, 0, NULL, &dw, NULL))
            return true;
        return dw > 0;
    } else {
        GetNumberOfConsoleInputEvents(inh, &dw);
        return dw > 1;
    }
#else
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
    return FD_ISSET(STDIN_FILENO, &readfds);
#endif
}

int GetMS() {

#if defined(_WIN32) || defined(_WIN64)
    return GetTickCount();
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

void POS::InitHashKey() {

    U64 key = 0;

    for (int i = 0; i < 64; i++)
        if (mPc[i] != NO_PC)
            key ^= msZobPiece[mPc[i]][i];

    key ^= msZobCastle[mCFlags];
    if (mEpSq != NO_SQ)
        key ^= msZobEp[File(mEpSq)];

    if (mSide == BC)
        key ^= SIDE_RANDOM;

    mHashKey = key;
}

void POS::InitPawnKey() {

    U64 key = 0;

    for (int i = 0; i < 64; i++) {
        if ((mTpBb[P] & SqBb(i)) || (mTpBb[K] & SqBb(i)))
            key ^= msZobPiece[mPc[i]][i];
    }

    mPawnKey = key;
}

void PrintMove(int move) {

    char moveString[6];
    MoveToStr(move, moveString);
    printfUciAdd("%s", moveString);
}

// returns internal static string. not thread safe!!!
std::string MoveToStr(int move) {

    char internalstring[6];
    MoveToStr(move, internalstring);
    return (std::string)internalstring;
}

void MoveToStr(int move, char *move_str) {

    static const char prom_char[5] = "nbrq";

    move_str[0] = File(Fsq(move)) + 'a';
    move_str[1] = Rank(Fsq(move)) + '1';
    move_str[2] = File(Tsq(move)) + 'a';
    move_str[3] = Rank(Tsq(move)) + '1';
    move_str[4] = '\0';

    // Bugfix by Dave Kaye for compatibility with Knights GUI (Linux) and UCI specs
    // (needed if a GUI forces the engine to analyse in checkmate/stalemate position)

    if (strcmp(move_str, "a1a1") == 0) {
        strcpy(move_str, "0000");
    }

    if (IsProm(move)) {
        move_str[4] = prom_char[(move >> 12) & 3];
        move_str[5] = '\0';
    }
}

int POS::StrToMove(const char *move_str) const {

    int from = Sq(move_str[0] - 'a', move_str[1] - '1');
    int to   = Sq(move_str[2] - 'a', move_str[3] - '1');
    int type = NORMAL;

    // change move type if necessary

    if ((TpOnSq(from) == K) && (TpOnSq(to) == R) && Cl(mPc[from]) == Cl(mPc[to])) {
        // Chess960 Castle
        type = CASTLE;
        if (move_str[2] > move_str[0])
            to   = Sq('g' - 'a', move_str[3] - '1');
        else
            to   = Sq('c' - 'a', move_str[3] - '1');
    }
    else if (TpOnSq(from) == K && (to == from)) {
        // Chess960 in Standard-Mode - special situation, only when king on c- or g-row
        type = CASTLE;
    }
    else if (TpOnSq(from) == K && Abs(to - from) >= 2 && move_str[1] == move_str[3])
        type = CASTLE;
    else if (TpOnSq(from) == P) {
        if (to == mEpSq)
            type = EP_CAP;
        else if (Abs(to - from) == 16)
            type = EP_SET;
        else if (move_str[4] != '\0')

            // record promotion piece

            switch (move_str[4]) {
                case 'n':
                    type = N_PROM;
                    break;
                case 'b':
                    type = B_PROM;
                    break;
                case 'r':
                    type = R_PROM;
                    break;
                case 'q':
                    type = Q_PROM;
                    break;
            }
    }

    // return move

    return (type << 12) | (to << 6) | from;
}

void cEngine::PvToStr(int *pv, char *pv_str) {

    int *movep;
    char move_str[6];

    pv_str[0] = '\0';
    for (movep = pv; *movep; movep++) {
        MoveToStr(*movep, move_str);
        strcat(pv_str, move_str);
        strcat(pv_str, " ");
    }
}

void cEngine::BuildPv(int *dst, int *src, int move) {

    *dst++ = move;
    while ((*dst++ = *src++))
        ;
}

void cEngine::WasteTime(int milliseconds) {

#if defined(_WIN32) || defined(_WIN64)
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000);
#endif
}

#if defined(_WIN32) || defined(_WIN64)
bool ChDir(const wchar_t *new_path) {
    bool result;
    std::wstring NewPathWStr;

    if (isabsolute(WCStr2Str(new_path).c_str()))
        NewPathWStr = WCStr2WStr(new_path);
    else
        NewPathWStr = RodentHomeDirWStr + WCStr2WStr(new_path);

    result = (SetCurrentDirectoryW(NewPathWStr.c_str()));

    if (result) {
        printf_debug("go to '%ls'\n", NewPathWStr.c_str());
    } else
        printf_debug("can't go to '%ls'\n", NewPathWStr.c_str());

    return result;
}
#else
void PrintOverrides() {

    if (char *ptr = getenv("RODENT4BOOKS"))
        printfUciOut("info string override for books path: '%s'\n", ptr);
    if (char *ptr = getenv("RODENT4PERSONALITIES"))
        printfUciOut("info string override for personalities path: '%s'\n", ptr);
}
#ifndef ANDROID
bool ChDirEnv(const char *env_name) {
    char *env_path;
    env_path = getenv(env_name);
    if (env_path == NULL) return false;

    printf_debug("env: %s = %s\n", env_name, env_path);

    wordexp_t p;
    switch (wordexp(env_path, &p, 0)) {
        case 0:
            break;
        case WRDE_NOSPACE:
            wordfree(&p);
        default:
            return false;
    }
    if (p.we_wordc != 1) { wordfree(&p); return false; }

    printf_debug("env: go to '%s'\n", p.we_wordv[0]);

    bool result = chdir(p.we_wordv[0]) == 0;
    wordfree(&p);

    return result;
}
#endif
bool ChDir(const char *new_path) {
    bool result;
    std::string NewPathStr;

    if (isabsolute(new_path))
        NewPathStr = new_path;
    else
        NewPathStr = WStr2Str(RodentHomeDirWStr) + new_path;

    result = (chdir(NewPathStr.c_str()) == 0);

    if (result) {
        printf_debug("go to '%s'\n", NewPathStr.c_str());
    } else
        printf_debug("can't go to '%s'\n", NewPathStr.c_str());

    return result;
}
#endif

#if (defined(_WIN32) || defined(_WIN64)) && __CYGWIN__
// In win-builds with cygwin, "fopen" is not opening files in current directory
// so you can use this function to get the FullPath of the file.
// (Not needed for other os, but still working)
std::string GetFullPath(const char *exe_file) {
    std::string FullPathFileStr;

    // As default the file itself, if already fullpath or current directory can't be determined
    FullPathFileStr = exe_file;

    if (!isabsolute(exe_file)) {
#if defined(_WIN32) || defined(_WIN64)
        char cwd[MAX_PATH + 1];

        if (GetCurrentDirectory(MAX_PATH + 1, cwd)) {
            FullPathFileStr = (std::string)cwd + "\\" + exe_file;
        }
#else
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            FullPathFileStr = (std::string)cwd + "/" + exe_file;
        }
#endif
    }

    return FullPathFileStr;
}
#endif

void printfLog(const char *preStr, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);

    if (strcmp(preStr, ">> ")) // uci-in needn't echo
        vfprintf(stdout, fmt, ap);

    if (LogFileWStr != L"") {

		if (Glob.isNoisy ||
		   (strstr(fmt, "info depth ")!=fmt && strstr(fmt, "info currmove ")!=fmt)) {

            FILE *logFile = NULL;
            logFile = fopen(WStr2Str(LogFileWStr).c_str(), "a+");
            if (logFile) {
                fprintf(logFile, "%s", preStr);
                vfprintf(logFile, fmt, ap);
                fclose(logFile);
            }
        }
    }

    va_end(ap);
}
