# 2020 Parallel Programming: Hw2

Table of Content
- [2020 Parallel Programming: Hw2](#2020-parallel-programming-hw2)
  - [Implementation 實作細節](#implementation-實作細節)
    - [Load Balancing (Block Resource)](#load-balancing-block-resource)
    - [Thread Worker](#thread-worker)
    - [Mandelbrot Set Calculation](#mandelbrot-set-calculation)
  - [Optimization](#optimization)
    - [基礎版本 (Worker1, Thread Level Parallelism)：Hw2a ScoreBoard 時間：770 秒左右](#基礎版本-worker1-thread-level-parallelismhw2a-scoreboard-時間770-秒左右)
    - [Vectorization v1.0 (Worker2, SIMD Programming, SSE)](#vectorization-v10-worker2-simd-programming-sse)
    - [Vectorization v2.0 (Worker3, SIMD Programming)：Hw2a ScoreBoard 時間：384 秒左右](#vectorization-v20-worker3-simd-programminghw2a-scoreboard-時間384-秒左右)
    - [Node 程式碼的其他優化 (hw2a)](#node-程式碼的其他優化-hw2a)
    - [Load Balancing (根據圖形特性得到的分法)：Hw2b ScoreBoard：364.54 -> 269.04 秒左右](#load-balancing-根據圖形特性得到的分法hw2b-scoreboard36454---26904-秒左右)
  - [Experiments 實驗結果](#experiments-實驗結果)
    - [實驗設備 & Testing Data Set](#實驗設備--testing-data-set)
    - [名詞定義](#名詞定義)
    - [Strong Scalability](#strong-scalability)
    - [Load Balancing](#load-balancing)
    - [Block Size Tune](#block-size-tune)
  - [Conclusion](#conclusion)
  - [Reference](#reference)

這次報告會分成三個部分來撰寫：
1. Implementation 實作細節：提到程式基本架構、 Load Balancing 的實作以及 Worker 的基本實作
2. Optimization 優化：所有從基本款程式碼優化到加速版本的細部介紹
3. Experiment 實驗結果：Strong Scalability, Load Balancing 的相關數據，以及額外 Tuning 實驗

## Implementation 實作細節

### Load Balancing (Block Resource)

經過判斷這次題目特性後，這次作業並沒有用到 Dynamic Load Balancing，而是使用長方形的切法，每個 Thread Processing 完自己的區塊後會再拿取下一個區塊，繼續執行（切的形狀其實也是可以優化的，後面優化的部分會再提到）。

```c++
// Load Balancing
while(true) {
    pthread_mutex_lock(&posLock);
    // Critical Section for Acquiring Workload.
    if (!getPosition(&iStart, &j, &localCount)) {
      pthread_mutex_unlock(&posLock);
      break;
    }

    pthread_mutex_unlock(&posLock);

    // .....
}

// getPosition()
bool getPosition(int * x, int * y, int * wid) {
  // Update Cursor and Return Wid to Worker
  if (xCursor + BLOCKSIZE > width) {
    *wid = width - xCursor;
    xCursor = 0;
    yCursor += 1;
  } else {
    *wid = BLOCKSIZE;
    xCursor += BLOCKSIZE;
  }
  return true;
}
```

當然這個實作方式（getPosition）Thread Worker 在取得新的區塊時，需要確保說同時只能有一個 Worker 在取得新的工作，所以這邊實作了一個簡單的 Mutex 保護著這塊 Critical Section，確保 Position 的資料不會有衝突。

### Thread Worker

雖然說我取名為 Worker，但我並沒有實作成 Master-Slave 的模式，每個 Thread 都是一個 Worker，分別執行自己拿取到的 Workload。Thread Worker 要做的事分為兩部分:
1. 固定讓自己不會是空閒狀態，只要自己手上沒有 Workload 的時候就會向 Block Resource 取得新的工作。
2. 計算自己手上目前有的資料，確認是不是存在 Mandelbrot Set 的 Pixel

Image 在讀寫的時候並不需要特別由一個 Mutex 進行保護，因為基本上每個 Thread 並不會同時寫到同一個記憶體區塊上。

### Mandelbrot Set Calculation

By 題目要求這次作業並不能對數學公式上做優化，所以每個 Pixel 的計算量是相同的，所以可以優化的部分就只剩下平行化的部分，不管是 Thread Level 的平行，還是 Data Level 的平行處理（Instruction Level 的話能處理的東西應該不多，所以這次作業基本上完全沒有思考這方面的優化）。

我總共寫了兩個版本的 Worker，分別可以達到不同的優化程度（優化的方法在後面有提到）：
1. 只實作 Thread Level 的平行處理，以及對一些程式碼架構的優化
2. 實作 Vectorization，因為單一 Pixel 之間的計算會有 Dependency，所以 Vectorized 的對象單位是 Pixel，透過一次處理兩個 Pixel 來達到加速的目的

## Optimization

這次兩個版本（hw2a, hw2b）的 Mandelbrot Set 計算中分別有兩個 Bottle Neck 需要處理，第一個是單個 Node 上的加速，也就是 Vectorization 的部分，第二個是 MPI 每個 Node 之間不同的 load balancing 工作量。

### 基礎版本 (Worker1, Thread Level Parallelism)：Hw2a ScoreBoard 時間：770 秒左右

這個版本非常的基礎，直接從 Sequential 版本中提出計算 Mandelbrot Set 的演算法放到 Thread Worker 的函式中，配上剛剛上面提到的簡單 Load Balancing 方法，就可以達到以上的效能。

### Vectorization v1.0 (Worker2, SIMD Programming, SSE)

Vectorization，又可以叫 SIMD Programming，看名字就知道是一種當有相同的工作需要套用在不同的資料上時可以實作的優化方向。根據 lab 上課提供的 Vectorization 講義來看，第一個會想到可以加速的地方是在 While 迴圈中的計算工作，透過把演算法中的計算配對成兩個兩個數值後丟到 Intrinsic 中計算，期待達到加速的效果。當然這種方法真的是慘不忍睹，因為光 load 以及 store 就會耗掉超多時間，從 compile 過後的 Assembly File 中可以看到執行大量的 movapd 指令。

經過上網瘋狂搜尋各個大學的講義以及社群中的文章之後，終於知道 Vectorization 應該要怎麼寫使用。第一個是 Union 的使用，透過 Union 可以對同一塊記憶體有不同解釋的特性，在每個 Interation 之間不必把資料 Load 到一般記憶體中就可以取用剛剛計算好的資料來做 If Else 判斷（Intrinsic 中其實也有許多 Compare 的指令，但我覺得處理 Bit Mask 有點太麻煩了所以就使用了 Union 的方式）；第二個是 _mm_set_pd 等 Function 有雷的特性，跟 __mm_load_pd 不一樣，Set 會把丟進去的值相反擺放，我因為這個雷浪費了很多時間在 Debug，上面，最後是在檢視輸出圖與標準答案之間的不同看到有很多鋸齒狀邊緣時才意識到可能是這個問題。再加上 Set 效能比 Load 慢後，我就換了寫法了。

```c++
// SIMD
// 128 bit
union PackTwoDouble {
  alignas(16) double d[2];
  __m128d d2;
};

// .....

// Update x, y
// Sequence Code Matter?
x.d2 = _mm_add_pd(_mm_sub_pd(xsquared, ysquared), x0.d2);
y.d2 = _mm_add_pd(_mm_mul_pd(_mm_mul_pd(tmpx, tmpy), const2.d2), y0.d2);

// .....
```

把 Vectorization 的底子修好以及該採的雷都採完後我做出了第一個有 Vectorized 版本的 Worker。第一個版本很 Naive，基本上就是兩個兩個 Pixel 做，兩個 Pixel 都要結束才會繼續讀入下兩個 Pixel 運算。寫完後執行 hw2a-judge 後結果如下：

![](./img/Screenshot%20from%202020-11-15%2019-12-45.png)*圖1, hw2a 以 worker2 版本 judge 出來的結果*

馬上就發現大問題了，先不說會有好幾筆答案有誤，某幾筆還會有執行時間變非常久的狀況（圖1 中的 Slow01）。經過思考後發現可能的原因是在於兩個 Channel（每個 register 可以放兩個 double，這篇文章中我將會稱某個 double 佔用的空間為 Channel）中，假設有一個已經先結束了（length_squared 大於 4），但礙於現在的設計並沒有清除這個 Channel 的值，這個 Channel 會被繼續計算下去，很有可能最後會 Overflow，導致資料開始計算有誤進而導致答案正確率不足。即使幸運的沒有出錯也會因為數字很大 Computation Latency 開始上升，效能也就呈現成 slow01 在圖 1 中的樣子了。

### Vectorization v2.0 (Worker3, SIMD Programming)：Hw2a ScoreBoard 時間：384 秒左右

最終版本跟上一個版本差距是在每個 Channel 的 load balancing。剛剛有提到上一個版本做法是兩個 pixel 為一個 Task 來計算，但這樣實作最大的缺點就是兩個 Pixel 只要不是同時結束就會導致其中一個 Channel 會浪費時間，所以這個版本做了一道設計，只要有其中一個 Channel 已經結束了就馬上吃入下一個 Pixel 進行計算，以達到兩個 Channel 效能最大應用。

```c++
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
```

最後，假設剩一個 Pixel 在計算，為了避免剛剛上面提到的問題，最後還會多一道手續把值提回一般 Register 中計算，最後再把資料寫回 buffer 中。

```c++
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
```

### Node 程式碼的其他優化 (hw2a)

其他小小的優化如 length_squared 確認改成在計算下一個 z 前先檢查，以減少原本 Sequential Version 中的重複計算 x^2 跟 y^2 的問題。

### Load Balancing (根據圖形特性得到的分法)：Hw2b ScoreBoard：364.54 -> 269.04 秒左右

這次我並沒有實作 Dynamic load balancing，原因是因為 write png 那邊不能優化成分散式寫入檔案，如果讓每個 Node 執行不一樣的計算會導致最後收回 rank 0 寫入 png 時會很難處理最後的寫入。在這個狀況下轉而開始研究 Static 的切法，最後根據圖的特性找出一個撰寫很簡單但效果顯著的切法。

| | |
| ---- | ---- |
|![](./img/strict26.png)*圖 2, Strict26 產出的標準圖像*  | ![Multiple Node Strong Scalability](./img/strict26-line.png)*圖 3, 採用高度平均分個的做法* |

圖 2 是 strict26 的測試資料所產出的結果。Mandelbrot Set 有個特性，就是附近的 Pixel 計算結果非常相近，所以如圖 3 所使用的方法去分割每個 MPI Process 的 Workload，極端的狀況下會讓某一個 Process 可能 2 秒就完成，但剩下的 Process 要花更久的時間把屬於自己的 Task 完成 （表1）。

| MPI Rank | Computation Time |
| -------- | ---------------- |
| 0 | 0.618309(sec) |
| 1 | 4.94486(sec) |
| 2 | 4.09803(sec) |
| 3 | 0.749039(sec) |

顧慮到 pixel 計算上的 locality，如果可以把附近計算量比較相同的特性納入考慮，讓每個 Process 都計算到這個區塊的一小部分，理論上這樣切割的方法就會讓每個 Node 更為平均，而實驗結果也是如此（見下面實驗結果章節）。最後找到一個方法，就是不再是大家平均分段，而是透過 height % size 的方法去分配給每個 Node，這樣的好處是在於每個 Node 都有參與每個區塊的運作，代表來說每個 Node 分配到的工作量會更平均，大家都有做到簡單的區塊也有做到計算量大的區塊。

當然這個狀況在 Hw2a 中並不明顯，因為每個 Thread 都可以很自由的取得所有地方的工作區塊，只有在 MPI 這種多 Node 中又沒有溝通時狀況才會非常明顯。

## Experiments 實驗結果

### 實驗設備 & Testing Data Set

使用的是課堂提供的機器(apollo)
- Data Setup 為 strict26 -> 10000 -0.19985997516420825 -0.19673850548118335 -1.0994739550641088 -1.-
- 1010040371755099 7680 4320
- GCC Compiler
- SSE SIMD Instruction
- 時間計算方法使用的是 clock_gettime with CLOCK_MONOTONIC

### 名詞定義

這次使用比較少的種類，只有兩種：

- Communication Time：用在紀錄 MPI 花在溝通傳資料的時間，只出現在 hw2b 中。
- Computation Time：用在紀錄單純計算 Mandelbrot Set 所花的時間，hw2a 跟 hw2b 都有紀錄。

比較需要注意的一點，這次實驗數據都沒有包含花在寫入 PNG 所花費的時間，原因是因為我只有優化計算的部份，如果把 PNG 寫入所花費的時間包含進來感覺沒有必要且圖表呈現會不太對。這次挑選的 Strict 26 資料是一筆花非常少時間在寫入 PNG 的資料組，所以感覺不包含 PNG 的問題也就比較沒那麼大。

### Strong Scalability

| | |
| ---- | ---- |
|![](./img/Screenshot%20from%202020-11-15%2019-42-50.png)*圖 4, 使用 1 ~ 4 個 Node 的結果*  | ![](./img/Screenshot%20from%202020-11-15%2011-23-37.png)*圖 5, 使用單 node 1 ~ 12 Thread 的結果* |

從圖中看起來 Scalability 真的有點太好了，連我自己都有一點不相信。當然 Sequential Version 我採用的並不是一般單個 Double 處理的結果，而是 Vectorized 過後以一個 CPU 來跑得結果當作基準值，不然這張圖的 Speedup Factor 應該會一開始就直接超 2 了。
會有這麼好的結果我覺的要歸功於 Load Balancing 做的還可以，從接下來的 Load Balancing 那邊的實驗數據來看，每一個 Node 和 Thread 都有很平均分配到相同的工作量，所以 Scalability 才會那麼好。

當然也是有少部份的地方不能平行，最大的 Overhead 應該就是在拿取下一個工作時的 lock 機制，這部份是不能平行的，所以曲線才會有些微往下。

###  Load Balancing

hw2a 跟 hw2b 的數據都顯示這個 Load Balancing 的做法效能還算不錯

![](img/Screen%20Shot%202020-12-11%20at%206.20.36%20PM.png)*圖 6, loadbalancing 實驗結果*

### Block Size Tune

比較好玩的是，每個工作要切的寬度有多大也可以 Tune。原因是在於說我的 load balancing 在拿新的 task 的時候會需要先拿一個 lock 以確保大家都會拿到正確的值，但如果今天每個 Task 切的太小會導致大家一直需要拿新的工作，locking 的 Overhead 會變得太高；但相對的，如果每個 Worker 一次拿到太多的 Task，這樣反而減少每個 Thread Share 工作量的程度，Performance 反而又開始下降了。

最後我測到最好的 Setting 是 width = 20，height = 1。

## Conclusion
這次作業主要是在考驗 DataSet 的計算特性的切割，最震撼的是在優化 hw2b 的時候，第一版我使用平均切的手法得出來的結果 Scoreboard 上 364 秒左右，但當我和朋友討論完後發現還有跳切這種手法且實作難度不高，做完 Scoreboard 直接到 269 秒上下，整整快了快 100 秒！我也只不過是改了差不多 10 行程式碼的事情，效能卻是天差地遠，真的是有點扯了。

## Reference
- Practical Guide of SIMD Programming [http://www.cs.uu.nl/docs/vakken/magr/2017-2018/files/SIMD%20Tutorial.pdf]
- Improving performance with SIMD intrinsics in three use cases [https://stackoverflow.blog/2020/07/08/improving-performance-with-simd-intrinsics-in-three-use-cases/]
- How to Write Fast Code SIMD Vectorization [https://users.ece.cmu.edu/~franzf/teaching/slides-18-645-simd.pdf]
- Performance Tuning for CPU - Part 1 SIMD Optimization [https://docs.google.com/presentation/d/1jq-MOwK1oMFr9iXoGJITBzA7bTeZ-roLDM38_5zR3Ek/htmlpresent]
- x86 and amd64 instruction reference [https://www.felixcloutier.com/x86/index.html]
- Developer Guide and Reference - Programming Guidelines for Vectorization [https://software.intel.com/content/www/us/en/develop/documentation/cpp-compiler-developer-guide-and-reference/top/optimization-and-programming-guide/vectorization/automatic-vectorization/programming-guidelines-for-vectorization.html]
