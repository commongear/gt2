# Copyright (c) 2021 commongear
# MIT License (see https://github.com/commongear/gt2/blob/master/LICENSE)

if [[ ("$OSTYPE" == "msys"*) ]] || [[ ("$OSTYPE" == "cygwin"*) ]]; then
  FILENAME=cdotool.exe
else
  FILENAME=cdotool
fi

clang++ --std=c++17 -O0 -g -fno-exceptions -Wall -Wextra -Werror \
    -Wno-unused-parameter \
    -Wno-unused-const-variable \
    -Wno-unused-variable \
    -Wno-sign-compare \
    -Wno-c++11-narrowing \
  cdotool.cpp -o $FILENAME
    # -fsanitize=address \
