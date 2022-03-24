cd "$(dirname "$0")" && mkdir -p bin && gcc -o bin/script util/script.c -g -Wall -Wextra -O2 -pthread && bin/script util/start.script options="`echo $@`"
