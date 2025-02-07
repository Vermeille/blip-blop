/******************************************************************
 *
 *
 *              -----------------
 *                 TxtData.cpp
 *              -----------------
 *
 *              Permet de charger les textes
 *
 *
 *              Prosper / LOADED -   V 0.1 - 17 Juillet 2000
 *
 *
 *
 ******************************************************************/

#define TXT_DATA_CPP

#include "txt_data.h"

#include <stdlib.h>
#include <string.h>
#include <fstream>

#include "ben_debug.h"

std::vector<std::string> txt_data;

bool loadTxtData(const char* file) {
    std::ifstream f(file);

    if (!f.is_open()) {
        return false;
    }

    std::string buffer;
    while (true) {
        std::getline(f, buffer, '^');

        if (f.eof()) {
            break;
        }

        int num = std::stoi(buffer);
        std::getline(f, buffer, '\n');

        if (num >= txt_data.size()) {
            txt_data.resize(num + 1);
        }
        txt_data[num] = buffer;
    }

    return true;
}
