#!/bin/sh

set -e

release=0
for arg in "$@"; do
	if [ "$arg" = "-release" ]; then
		release=1
	fi
done

echo Fetching mongoose.
curl -LO https://raw.githubusercontent.com/cesanta/mongoose/refs/heads/master/mongoose.c
curl -LO https://raw.githubusercontent.com/cesanta/mongoose/refs/heads/master/mongoose.h
mv -v mongoose.c src/
mv -v mongoose.h src/

echo Fetching JSON library.
curl -LO https://raw.githubusercontent.com/sheredom/json.h/refs/heads/master/json.h
mv -v json.h src/

echo Building library.

optimization_options="-O0 -g"
if [ "$release" -eq 1 ]; then
	optimization_options="-O3"
fi

cc -c -Wall -Wextra $optimization_options -o libneurosdk.o -I include src/neurosdk.c

