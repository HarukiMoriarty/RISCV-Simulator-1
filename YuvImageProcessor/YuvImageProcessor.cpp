/*
 * Read an YUV file, do a number of image transformations using basic ISA, MMX,
 * SE2, and AVX, and do a performance evaluation.
 *
 * Created by He, Hao at 2019/05/08
 */

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <immintrin.h>
#include <nmmintrin.h>

struct Pixel {
  uint8_t r, g, b, a;
};

struct Image {
  int h;
  int w;
  struct Pixel *data;
};

inline float absf(float a) { return a > 0 ? a : -a; }

inline int clamp(int val, int min, int max) {
  return val < min ? min : (val > max ? max : val);
}

inline void print_mm128(__m128 a) {
  printf("%f %f %f %f\n", a[0], a[1], a[2], a[3]);
}

bool parseArguments(int argc, char **argv);
void printUsage();
void measureTime(void func(), const char *desc);
void processYuv();
void processYuvMMX();
void processYuvSSE2();
void processYuvAVX();

char *inFilePath = nullptr;
char *outFilePath = nullptr;
int width = 0;
int height = 0;
char *yuv;
const int NUM_FRAMES = 84;
char *result[NUM_FRAMES];

int main(int argc, char **argv) {
  if (!parseArguments(argc, argv)) {
    printUsage();
    exit(-1);
  }

  // Read yuv files and initialize data structures
  std::ifstream infile(inFilePath);
  infile.seekg(0, infile.end);
  size_t length = infile.tellg(); // get length of file
  if (length < width * height * 6 / 4) {
    fprintf(stderr, "File length did not match image width and height\n");
    exit(-1);
  }
  infile.seekg(0, infile.beg);
  yuv = new char[length];
  for (int i = 0; i < NUM_FRAMES; ++i) {
    result[i] = new char[length];
  }
  infile.read(yuv, length); // read file
  infile.close();

  printf("File length: %zu bytes\n", length);
  printf("Image: %d * %d YUV420 Format\n", width, height);

  // Process image in different ways
  measureTime(processYuv, "Basic");
  // measureTime(processYuvMMX, "MMX");
  measureTime(processYuvSSE2, "SSE2");
  measureTime(processYuvAVX, "AVX");

  // Write result to file
  std::ofstream outfile(outFilePath);
  for (int i = 0; i < NUM_FRAMES; ++i) {
    outfile.write(result[i], length);
  }
  outfile.close();

  // Release data structures
  delete yuv;
  for (int i = 0; i < NUM_FRAMES; ++i) {
    delete result[i];
  }
  return 0;
}

bool parseArguments(int argc, char **argv) {
  if (argc < 8) {
    return false;
  }

  // Read Parameters
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') {
      switch (argv[i][1]) {
      case 'w':
        if (i + 1 < argc) {
          width = atoi(argv[++i]);
        } else {
          return false;
        }
        break;
      case 'h':
        if (i + 1 < argc) {
          height = atoi(argv[++i]);
        } else {
          return false;
        }
        break;
      case 'o':
        if (i + 1 < argc) {
          outFilePath = argv[++i];
        } else {
          return false;
        }
        break;
      default:
        return false;
      }
    } else {
      if (inFilePath == nullptr) {
        inFilePath = argv[i];
      } else {
        return false;
      }
    }
  }
  if (inFilePath == nullptr) {
    return false;
  }
  return true;
}

void printUsage() {
  fprintf(
      stderr,
      "Usage: YuvImageProcessor yuv-file -w width -h height -o output-file\n");
  fprintf(stderr, "Parameters: yuv-file the path to the input yuv file\n");
  fprintf(stderr, "            -w width the image width of the yuv file\n");
  fprintf(stderr, "            -h height the image height of the yuv file\n");
  fprintf(stderr, "            -o output-file the path to output file\n");
}

void measureTime(void func(), const char *desc) {
  auto start = std::chrono::high_resolution_clock::now();
  func();
  auto finish = std::chrono::high_resolution_clock::now();
  auto interval =
      std::chrono::duration_cast<std::chrono::milliseconds>(finish - start);
  printf("%s: \t%lldms\n", desc, interval.count());
}

/*
 * RGB to YUV Conversion
 * R = 1.164383 * (Y - 16) + 1.596027*(V - 128)
 * G = 1.164383 * (Y - 16) – 0.391762*(U - 128) – 0.812968*(V - 128)
 * B = 1.164383 * (Y - 16) + 2.017232*(U - 128)
 *
 * RGB to YUV Conversion
 * Y =  0.256788*R + 0.504129*G + 0.097906*B + 16
 * U = -0.148223*R - 0.290993*G + 0.439216*B + 128
 * V = 0.439216*R - 0.367788*G - 0.071427*B + 128
 */
void processYuv() {
  int total = width * height;
  Pixel *data = new Pixel[total];

  for (int i = 0; i < height; ++i) {
    for (int j = 0; j < width; ++j) {
      int offset = i * width + j;
      int y = (uint8_t)yuv[offset];
      int u = (uint8_t)yuv[(i / 2) * (width / 2) + (j / 2) + total];
      int v =
          (uint8_t)yuv[(i / 2) * (width / 2) + (j / 2) + total + (total / 4)];
      int r = 1.164383 * (y - 16) + 1.596027 * (v - 128);
      int g = 1.164383 * (y - 16) - 0.391762 * (u - 128) - 0.812968 * (v - 128);
      int b = 1.164383 * (y - 16) + 2.017232 * (u - 128);
      data[offset].r = clamp(r, 0, 255);
      data[offset].g = clamp(g, 0, 255);
      data[offset].b = clamp(b, 0, 255);
    }
  }

  for (int num = 0; num < NUM_FRAMES; ++num) {
    int a = num * 3 + 1;
    for (int i = 0; i < height; ++i) {
      for (int j = 0; j < width; ++j) {
        int offset = i * width + j;
        int r = data[offset].r * a / 255.0;
        int g = data[offset].g * a / 255.0;
        int b = data[offset].b * a / 255.0;
        int y = 0.256788 * r + 0.504129 * g + 0.097906 * b + 16;
        int u = -0.148223 * r - 0.290993 * g + 0.439216 * b + 128;
        int v = 0.439216 * r - 0.367788 * g - 0.071427 * b + 128;
        result[num][offset] = y;
        result[num][(i / 2) * (width / 2) + (j / 2) + total] = u;
        result[num][(i / 2) * (width / 2) + (j / 2) + total + (total / 4)] = v;
      }
    }
  }

  delete[] data;
}

void processYuvMMX() {
  int total = width * height;
  Pixel *data = new Pixel[total];

  for (int i = 0; i < height; ++i) {
    for (int j = 0; j < width; ++j) {
      int offset = i * width + j;
      int y = (uint8_t)yuv[offset];
      int u = (uint8_t)yuv[(i / 2) * (width / 2) + (j / 2) + total];
      int v =
          (uint8_t)yuv[(i / 2) * (width / 2) + (j / 2) + total + (total / 4)];
      int r = 1.164383 * (y - 16) + 1.596027 * (v - 128);
      int g = 1.164383 * (y - 16) - 0.391762 * (u - 128) - 0.812968 * (v - 128);
      int b = 1.164383 * (y - 16) + 2.017232 * (u - 128);
      data[offset].r = clamp(r, 0, 255);
      data[offset].g = clamp(g, 0, 255);
      data[offset].b = clamp(b, 0, 255);
    }
  }

  for (int num = 0; num < NUM_FRAMES; ++num) {
    int a = num * 3 + 1;
    for (int i = 0; i < height; ++i) {
      for (int j = 0; j < width; ++j) {
        int offset = i * width + j;
        int r = data[offset].r * a / 255.0;
        int g = data[offset].g * a / 255.0;
        int b = data[offset].b * a / 255.0;
        int y = 0.256788 * r + 0.504129 * g + 0.097906 * b + 16;
        int u = -0.148223 * r - 0.290993 * g + 0.439216 * b + 128;
        int v = 0.439216 * r - 0.367788 * g - 0.071427 * b + 128;
        result[num][offset] = y;
        result[num][(i / 2) * (width / 2) + (j / 2) + total] = u;
        result[num][(i / 2) * (width / 2) + (j / 2) + total + (total / 4)] = v;
      }
    }
  }

  delete[] data;
}

void processYuvSSE2() {
  int total = width * height;
  struct Vec {
    float a[4];
  } *data = new Vec[total];

  for (int i = 0; i < height; ++i) {
    for (int j = 0; j < width; ++j) {
      int offset = i * width + j;
      int uIndex = (i / 2) * (width / 2) + (j / 2) + total;
      int vIndex = (i / 2) * (width / 2) + (j / 2) + total + (total / 4);

      __m128 t0 = _mm_set_ps(0.0f, (uint8_t)yuv[vIndex], (uint8_t)yuv[uIndex],
                             (uint8_t)yuv[offset]);
      __m128 t1 = _mm_sub_ps(t0, _mm_set_ps(0.0f, 128.0f, 128.0f, 16.0f));
      __m128 t2 = _mm_set_ps(0.0f, 1.596027f, 0.0f, 1.164383f);
      __m128 t3 = _mm_set_ps(0.0f, -0.812968f, -0.391762f, 1.164383f);
      __m128 t4 = _mm_set_ps(0.0f, 0.0f, 2.017232f, 1.164383f);
      __m128 t5 = _mm_dp_ps(t1, t2, 0b11110001);
      __m128 t6 = _mm_dp_ps(t1, t3, 0b11110010);
      __m128 t7 = _mm_dp_ps(t1, t4, 0b11110100);
      __m128 rgb = _mm_add_ps(_mm_add_ps(t5, t6), t7);
      __m128 zero = _mm_set_ps(0.0f, 0.0f, 0.0f, 0.0f);
      __m128 max = _mm_set_ps(255.0f, 255.0f, 255.0f, 255.0f);
      rgb = _mm_max_ps(rgb, zero);
      rgb = _mm_min_ps(rgb, max);
      _mm_store_ps(data[offset].a, rgb);
    }
  }

  for (int num = 0; num < NUM_FRAMES; ++num) {
    int a = num * 3 + 1;
    for (int i = 0; i < height; ++i) {
      for (int j = 0; j < width; ++j) {
        int offset = i * width + j;
        int uIndex = (i / 2) * (width / 2) + (j / 2) + total;
        int vIndex = (i / 2) * (width / 2) + (j / 2) + total + (total / 4);

        __m128 t0 = _mm_set_ps(0.0f, a, a, a);
        __m128 t1 = _mm_div_ps(t0, _mm_set_ps(255.0f, 255.0f, 255.0f, 255.0f));
        __m128 rgb = _mm_mul_ps(t1, _mm_load_ps(data[offset].a));
        __m128 yc = _mm_set_ps(0.0f, 0.097906f, 0.504129f, 0.256788f);
        __m128 uc = _mm_set_ps(0.0f, 0.439126f, -0.290993f, -0.148223f);
        __m128 vc = _mm_set_ps(0.0f, -0.071427f, -0.367788f, 0.439216f);
        __m128 y = _mm_dp_ps(rgb, yc, 0b11110001);
        __m128 u = _mm_dp_ps(rgb, uc, 0b11110010);
        __m128 v = _mm_dp_ps(rgb, vc, 0b11110100);
        __m128 t2 = _mm_add_ps(_mm_add_ps(y, _mm_add_ps(u, v)),
                               _mm_set_ps(0.0f, 128.0f, 128.0f, 16.0f));
        __m128i t2i = _mm_cvtps_epi32(t2);

        result[num][offset] = _mm_extract_epi32(t2i, 0);
        result[num][uIndex] = _mm_extract_epi32(t2i, 1);
        result[num][vIndex] = _mm_extract_epi32(t2i, 2);
      }
    }
  }

  delete[] data;
}

void processYuvAVX() {}