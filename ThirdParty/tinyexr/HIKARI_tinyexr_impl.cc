// tinyexr の実装を 1 か所だけ持つ翻訳単位（miniz は miniz.c を別にコンパイルする）。
#define TINYEXR_IMPLEMENTATION
#define TINYEXR_USE_MINIZ 1
#define TINYEXR_USE_THREAD 0
#define TINYEXR_USE_OPENMP 0
#include "tinyexr.h"
