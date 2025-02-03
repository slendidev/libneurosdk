#!/bin/sh

set -e

VERSION="v0.1.0"

release=0
example=0
for arg in "$@"; do
	if [ "$arg" = "-release" ]; then
		release=1
	fi
	if [ "$arg" = "-example" ]; then
		example=1
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

optimization_options="-O0 -g"
if [ "$release" -eq 1 ]; then
	optimization_options="-O3"
fi

echo Building library.

cc -c \
	-o libneurosdk.o \
	-Wall -Wextra \
	$optimization_options \
	"-DLIB_BUILD_HASH=$(git rev-parse HEAD)" \
	"-DLIB_VERSION=$VERSION" \
	-I include \
	src/neurosdk.c

if [ "$(uname)" = "Darwin" ]; then
	shared_lib="libneurosdk.dylib"
	shared_flags="-dynamiclib"
else
	shared_lib="libneurosdk.so"
	shared_flags="-shared"
fi

echo Building shared library
cc $shared_flags -o $shared_lib libneurosdk.o

echo Building static library
ar rcs libneurosdk.a libneurosdk.o

echo "Built $shared_lib and libneurosdk.a"

if [ "$example" -eq 1 ]; then
	echo Building examples.

	cc \
		-o examples/simple \
		-Wall -Wextra \
		$optimization_options \
		-Iinclude \
		examples/simple.c \
		libneurosdk.a

	cc \
		-o examples/tictactoe \
		-Wall -Wextra \
		$optimization_options \
		-Iinclude \
		examples/tictactoe.c \
		libneurosdk.a
fi
