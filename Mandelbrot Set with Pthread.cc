#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define PNG_NO_SETJMP
#include <sched.h>
#include <assert.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <iostream>
#include <emmintrin.h>

// Global Share Setting (Read Only)
double yInterval, xInterval;
const double ZERO = 0;
const double TWO = 2;
// 可能會有 Width 小於 Block Size 的狀況，到時候再來處理 Edge Cases
// 為什麼小於 500 的 Block Size 會錯這麼多 沒事我笨
const int BLOCKSIZE = 20;
int width, height, iters, * image;
double left, right, lower, upper;

// Share Resource
int xCursor = 0, yCursor = 0;
pthread_mutex_t posLock = PTHREAD_MUTEX_INITIALIZER;
bool getPosition(int * x, int * y, int * width);

// Function Declaraction
void write_png(const char* filename, int iters, int width, int height, const int* buffer);
void * worker(void * arg);
void * worker2(void * arg);
void * worker3(void * arg);

// SIMD
// 128 bit
union PackTwoDouble {
  alignas(16) double d[2];
  __m128d d2;
};

int main(int argc, char** argv) {
    /* detect how many CPUs are available */
    cpu_set_t cpu_set;
    sched_getaffinity(0, sizeof(cpu_set), &cpu_set);
    int ncpus = CPU_COUNT(&cpu_set);

    /* argument parsing */
    assert(argc == 9);
    const char* filename = argv[1];
    iters = strtol(argv[2], 0, 10);
    left = strtod(argv[3], 0);
    right = strtod(argv[4], 0);
    lower = strtod(argv[5], 0);
    upper = strtod(argv[6], 0);
    width = strtol(argv[7], 0, 10);
    height = strtol(argv[8], 0, 10);

    // This segament of codes can be vectorized. Expects Not much improvement on performance
    yInterval = ((upper - lower) / height);
    xInterval = ((right - left) / width);

    /* allocate memory for image */
    image = (int*)malloc(width * height * sizeof(int));
    assert(image);

    pthread_t threads[ncpus];

    for (int i = 0; i < ncpus; i++) {
      pthread_create(&threads[i], NULL, worker3, NULL);
    }

    for (int i = 0; i < ncpus; i++) {
      pthread_join(threads[i], NULL);
    }

    /* draw and cleanup */
    write_png(filename, iters, width, height, image);
    free(image);
}

// SIMD Version with Balancing
void * worker3(void * arg) {
  int iStart, j, localCount, repeats[2], curXCursor[2], xCursor;
  double x0Buffer[2], y0Buffer;

  // Setup Constant
  PackTwoDouble const2;
  const2.d2 = _mm_load1_pd(&TWO);

  while(true) {
    pthread_mutex_lock(&posLock);
    // Critical Section for Acquiring Workload.
    if (!getPosition(&iStart, &j, &localCount)) {
      pthread_mutex_unlock(&posLock);
      break;
    }
    // std::cout << "Get: x: " << iStart << " y:" << j << " count: " << localCount << std::endl;
    pthread_mutex_unlock(&posLock);

    // Process Resource
    PackTwoDouble x0, y0, x, y, lengthSquared;

    // Set y0 to both part of register
    y0Buffer = j * yInterval + lower;
    y0.d2 = _mm_load_pd1(&y0Buffer);

    // Prepare First Payload
    // Set x0 to X0Buffer
    // 如果 Width 小於 1 可能要考慮一下
    curXCursor[0] = iStart;
    curXCursor[1] = iStart + 1;
    x0Buffer[0] = curXCursor[0] * xInterval + left;
    x0Buffer[1] = curXCursor[1] * xInterval + left;
    xCursor = iStart + 2;

    // Init SIMD
    repeats[0] = repeats[1] = 0;
    lengthSquared.d2 = x.d2 = y.d2 = _mm_setzero_pd();
    x0.d2 = _mm_load_pd(x0Buffer);

    while(curXCursor[0] < iStart + localCount && curXCursor[1] < iStart + localCount) {
      // Inner Working Loop
      // 越簡單越好
      while(true) {
        // Tmp x, y
        __m128d tmpx = x.d2, tmpy = y.d2, xsquared, ysquared;

        // Check Length Squared
        xsquared = _mm_mul_pd(tmpx, tmpx);
        ysquared = _mm_mul_pd(tmpy, tmpy);

        lengthSquared.d2 = _mm_add_pd(xsquared, ysquared);

        if (lengthSquared.d[0] > 4 || lengthSquared.d[1] > 4) break;

        // Update x, y
        // Sequence Code Matter?
        x.d2 = _mm_add_pd(_mm_sub_pd(xsquared, ysquared), x0.d2);
        y.d2 = _mm_add_pd(_mm_mul_pd(_mm_mul_pd(tmpx, tmpy), const2.d2), y0.d2);

        // Update Repeats Count
        repeats[0]++;
        repeats[1]++;

        if (repeats[0] >= iters || repeats[1] >= iters) break;
      }

      // Write and Prepare for Next Iteration
      if (lengthSquared.d[0] > 4 || repeats[0] >= iters) {
        image[j * width + curXCursor[0]] = repeats[0];

        // Get Next Squared
        repeats[0] = 0;
        curXCursor[0] = xCursor++;
        x0Buffer[0] = curXCursor[0] * xInterval + left;

        // Update x, y
        x.d2 = _mm_loadl_pd(x.d2, &ZERO);
        y.d2 = _mm_loadl_pd(y.d2, &ZERO);
        lengthSquared.d2 = _mm_loadl_pd(lengthSquared.d2, &ZERO);
      }

      if (lengthSquared.d[1] > 4 || repeats[1] >= iters) {
        image[j * width + curXCursor[1]] = repeats[1];

        // Get Next Squared
        repeats[1] = 0;
        curXCursor[1] = xCursor++;
        x0Buffer[1] = curXCursor[1] * xInterval + left;

        // Update x, y
        x.d2 = _mm_loadh_pd(x.d2, &ZERO);
        y.d2 = _mm_loadh_pd(y.d2, &ZERO);
        lengthSquared.d2 = _mm_loadh_pd(lengthSquared.d2, &ZERO);
      }

      x0.d2 = _mm_load_pd(x0Buffer);
      // std::cout << "I: " << i + iStart << " J: " << j << " Iter: " << repeats[0] << " " << repeats[1] << std::endl;
    }
    // 處理剩餘沒處理完的
    if (curXCursor[0] < iStart + localCount) {
       while (repeats[0] < iters && lengthSquared.d[0] < 4) {
          double temp = x.d[0] * x.d[0] - y.d[0] * y.d[0] + x0.d[0];
          y.d[0] = 2 * x.d[0] * y.d[0] + y0.d[0];
          x.d[0] = temp;
          lengthSquared.d[0] = x.d[0] * x.d[0] + y.d[0] * y.d[0];
          ++repeats[0];
      }
      image[j * width +curXCursor[0]] = repeats[0];
    }
    if (curXCursor[1] < iStart + localCount) {
       while (repeats[1] < iters && lengthSquared.d[1] < 4) {
          double temp = x.d[1] * x.d[1] - y.d[1] * y.d[1] + x0.d[1];
          y.d[1] = 2 * x.d[1] * y.d[1] + y0.d[1];
          x.d[1] = temp;
          lengthSquared.d[1] = x.d[1] * x.d[1] + y.d[1] * y.d[1];
          ++repeats[1];
      }
      image[j * width +curXCursor[1]] = repeats[1];
    }
  }

  return NULL;
}

// SIMD Version
void * worker2(void * arg) {
  int iStart, j, localCount, repeats[2];
  double x0Start;

  // Setup Constant
  PackTwoDouble const2;
  const2.d2 = _mm_set1_pd(2);

  while(true) {
    pthread_mutex_lock(&posLock);
    // Critical Section for Acquiring Workload.
    if (!getPosition(&iStart, &j, &localCount)) {
      pthread_mutex_unlock(&posLock);
      break;
    }
    pthread_mutex_unlock(&posLock);

    // Process Resource
    PackTwoDouble x0, y0, x, y, lengthSquared;

    // Set y0 to both part of register
    y0.d2 = _mm_set_pd1(j * yInterval + lower);
    x0Start = iStart * xInterval + left;

    // 等等 Write Img 的時候要 Check 不要寫道 Dummy Data
    for (int i = 0; i < localCount; i+=2) {
      // 把資料寫進 x0 中
      // Note: 這裡要寫相反，到時候用 Array 取值的時候才不會拿反
      x0.d2 = _mm_set_pd(x0Start + xInterval * (i+1), x0Start + xInterval * i);

      // 初始化所有資料
      repeats[0] = repeats[1] = 0;
      lengthSquared.d2 = x.d2 = y.d2 = _mm_setzero_pd();

      while ((repeats[0] < iters && repeats[1] < iters) && (lengthSquared.d[0] < 4 || lengthSquared.d[1] < 4)) {
        // Tmp x, y
        __m128d tmpx = x.d2, tmpy = y.d2;

        // Update Repeats Count
        if (lengthSquared.d[0] < 4) repeats[0]++;
        if (lengthSquared.d[1] < 4) repeats[1]++;

        // Update x, y
        // Sequence Code Matter?
        x.d2 = _mm_add_pd(_mm_sub_pd(_mm_mul_pd(tmpx, tmpx), _mm_mul_pd(tmpy, tmpy)), x0.d2);
        y.d2 = _mm_add_pd(_mm_mul_pd(_mm_mul_pd(tmpx, tmpy), const2.d2), y0.d2);

        // Check Length Squared
        lengthSquared.d2 = _mm_add_pd(_mm_mul_pd(x.d2, x.d2), _mm_mul_pd(y.d2, y.d2));

        // if (i + iStart == 1830 && j == 0) std::cout << "Iter: " <<  repeats[0] << " | x: " << x.d[0] << " | y: " << y.d[0] << " | SquaredLength: " << lengthSquared.d[0] << std::endl;
      }

      // 要 double check 這個 i 是不是合法值
      if (i < localCount) image[j * width + i + iStart] = repeats[0];
      if (i + 1 < localCount) image[j * width + i + 1 + iStart] = repeats[1];
      // std::cout << "I: " << i + iStart << " J: " << j << " Iter: " << repeats[0] << " " << repeats[1] << std::endl;
    }
  }

  return NULL;
}

void * worker(void * arg) {
  int iStart, j, localCount;

  while(true) {
    pthread_mutex_lock(&posLock);
    // Critical Section for Acquiring Workload.
    if (!getPosition(&iStart, &j, &localCount)) {
      pthread_mutex_unlock(&posLock);
      break;
    }
    pthread_mutex_unlock(&posLock);

    // Process Resource
    double y0 = j * yInterval + lower;
    double x0 = (iStart - 1) * xInterval + left;
    for (int i = 0; i < localCount; ++i) {
      x0 += xInterval;
      int repeats = 0;
      double x = 0;
      double y = 0;
      double length_squared = 0;
      while (repeats < iters && length_squared < 4) {
          double temp = x * x - y * y + x0;
          y = 2 * x * y + y0;
          x = temp;
          length_squared = x * x + y * y;
          ++repeats;
          // if (i + iStart == 1830 && j == 0) std::cout << "Iter: " <<  repeats << " | x: " << x << " | y: " << y << " | SquaredLength: " << length_squared << std::endl;
      }
      image[j * width + i + iStart] = repeats;
    }
  }

  return NULL;
}

bool getPosition(int * x, int * y, int * wid) {
  // Check if There is still work to accomplish.
  if (yCursor >= height) return false;

  // Return X, Y to Worker
  *x = xCursor;
  *y = yCursor;
  
  // Update Cursor and Return Wid to Worker
  if (xCursor + BLOCKSIZE > width) {
    *wid = width - xCursor;
    xCursor = 0;
    yCursor += 1;
  } else {
    *wid = BLOCKSIZE;
    xCursor += BLOCKSIZE;
  }

  // std::cout << "[GetPosition]: X: " << *x << " | Y: " << *y << " | Wid: " << *wid << std::endl;

  return true;
}

void write_png(const char* filename, int iters, int width, int height, const int* buffer) {
    FILE* fp = fopen(filename, "wb");
    assert(fp);
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    assert(png_ptr);
    png_infop info_ptr = png_create_info_struct(png_ptr);
    assert(info_ptr);
    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_filter(png_ptr, 0, PNG_NO_FILTERS);
    png_write_info(png_ptr, info_ptr);
    png_set_compression_level(png_ptr, 1);
    size_t row_size = 3 * width * sizeof(png_byte);
    png_bytep row = (png_bytep)malloc(row_size);
    for (int y = 0; y < height; ++y) {
        memset(row, 0, row_size);
        for (int x = 0; x < width; ++x) {
            int p = buffer[(height - 1 - y) * width + x];
            png_bytep color = row + x * 3;
            // std::cout << "Position: [" << x << ", " << (height - 1 - y) << "]. Iter: " << p << std::endl;
            if (p != iters) {
                if (p & 16) {
                    color[0] = 240;
                    color[1] = color[2] = p % 16 * 16;
                } else {
                    color[0] = p % 16 * 16;
                }
            }
        }
        png_write_row(png_ptr, row);
    }
    free(row);
    png_write_end(png_ptr, NULL);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
}