#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <chrono>
#include <ctime>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

using namespace std;

// Block vars
int HEIGHT;
int WIDTH;
int BLOCKSIZE;
int H_COUNT;
int W_COUNT;
bool PROCESS_INIT = false;
int LINE_COUNT = 0;
// Computation vars
int SUBSTEPS;
float *CONDUCT;
float *TEMP;
float *TEMP2;
// Thread vars
typedef struct Task {
    int i = -1;
    int j = -1;
    int x = -1;
    int y = -1;
    int x_end = -1;
    int y_end = -1;
} Task;

std::vector<std::vector<Task>> TASK_Q;

inline int idx(int x, int y) { return y * WIDTH + x; }

inline int minu(int x, int y) { return (x < y) ? x : y; }

inline int min(int x, int y, int z) { return minu(minu(x, y), z); }

inline int max(int x, int y) { return (x > y) ? x : y; }

inline void update() { std::swap(TEMP, TEMP2); }

void process_edge();
void process_edge_avx();
void process_mid(int x, int y, int x_end, int y_end);
void process_mid_avx(int x, int y, int x_end, int y_end);

class ThreadPool {
   public:
    ThreadPool(size_t threads) : busy(0), stop(false) {
        for (size_t i = 0; i < threads; i++)
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->condition.wait(lock, [this] {
                        return this->stop || !this->tasks.empty();
                    });
                    if (this->stop && this->tasks.empty()) return;
                    // get fist task of the task queue
                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                    busy++;
                    lock.unlock();
                    task();
                    lock.lock();
                    busy--;
                    cv_finished.notify_one();
                }
            });
    }
    template <class F, class... Args>
    auto enqueue(F &&f, Args &&...args)
        -> std::future<typename std::result_of<F(Args...)>::type> {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            // don't allow enqueueing after stopping the pool
            if (stop) throw std::runtime_error("enqueue on stopped ThreadPool");

            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }
    // push tasks
    void push_edge_task() { this->enqueue(process_edge_avx); }
    void push_midblocks_task() {
        for (size_t i = 0; i < TASK_Q.size(); i++) {
            std::vector<Task> this_line_task = TASK_Q[i];
            for (size_t j = 0; j < this_line_task.size(); j++) {
                Task t = this_line_task[j];  // block found
                int block_x = t.x_end - t.x + 1;
                if (block_x % 8 == 0) {
                    this->enqueue(process_mid_avx, t.x, t.y, t.x_end, t.y_end);
                    // this->enqueue(process_mid, t.x, t.y, t.x_end, t.y_end);
                } else {
                    this->enqueue(process_mid, t.x, t.y, t.x_end, t.y_end);
                }
            }
        }
    }
    void push_update_task() { this->enqueue(update); }

    // for dependencies
    void wait_for_all_tasks() {
        std::unique_lock<std::mutex> lock(queue_mutex);
        cv_finished.wait(lock,
                         [this]() { return tasks.empty() && (busy == 0); });
    }
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread &worker : workers) worker.join();
    }

   private:
    // need to keep track of threads so we can join them
    std::vector<std::thread> workers;
    // the task queue
    std::queue<std::function<void()>> tasks;

    // synchronization
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::condition_variable cv_finished;
    unsigned int busy;
    bool stop;
};

void stencil(int i, int j) {
    const int left = idx(i - 1, j);
    const int right = idx(i + 1, j);
    const int up = idx(i, j - 1);
    const int down = idx(i, j + 1);
    const int mid = idx(i, j);

    float t = TEMP[mid];
    TEMP2[mid] =
        t +
        ((TEMP[left] - t) * CONDUCT[left] + (TEMP[right] - t) * CONDUCT[right] +
         (TEMP[up] - t) * CONDUCT[up] + (TEMP[down] - t) * CONDUCT[down]) *
            0.2;
}

void stencil_avx(int i, int j) {
    const int left = idx(i - 1, j);
    const int right = idx(i + 1, j);
    const int up = idx(i, j - 1);
    const int down = idx(i, j + 1);
    const int mid = idx(i, j);

    // load temp t = TEMP[mid]
    const __m256 t = _mm256_loadu_ps(&TEMP[mid]);
    // TEMP[left] - t
    const __m256 temp_left = _mm256_sub_ps(_mm256_loadu_ps(&TEMP[left]), t);
    // TEMP[right] - t
    const __m256 temp_right = _mm256_sub_ps(_mm256_loadu_ps(&TEMP[right]), t);
    // TEMP[up] - t
    const __m256 temp_up = _mm256_sub_ps(_mm256_loadu_ps(&TEMP[up]), t);
    // TEMP[down] - t
    const __m256 temp_down = _mm256_sub_ps(_mm256_loadu_ps(&TEMP[down]), t);

    const __m256 cond_left = _mm256_loadu_ps(&CONDUCT[left]);
    const __m256 cond_right = _mm256_loadu_ps(&CONDUCT[right]);
    const __m256 cond_up = _mm256_loadu_ps(&CONDUCT[up]);
    const __m256 cond_down = _mm256_loadu_ps(&CONDUCT[down]);
    // (TEMP[left] - t) * CONDUCT[left]
    const __m256 temp_left_cond = _mm256_mul_ps(temp_left, cond_left);
    // (TEMP[right] - t) * CONDUCT[right]
    const __m256 temp_right_cond = _mm256_mul_ps(temp_right, cond_right);
    // (TEMP[up] - t) * CONDUCT[up]
    const __m256 temp_up_cond = _mm256_mul_ps(temp_up, cond_up);
    // (TEMP[down] - t) * CONDUCT[down]
    const __m256 temp_down_cond = _mm256_mul_ps(temp_down, cond_down);

    const __m256 accumulate1 = _mm256_add_ps(temp_left_cond, temp_right_cond);
    const __m256 accumulate2 = _mm256_add_ps(temp_up_cond, temp_down_cond);
    const __m256 sum = _mm256_add_ps(accumulate1, accumulate2);
    // set all elements of scaler vector to 0.2
    const __m256 scale = _mm256_set1_ps(0.2);

    // result = (sum * scale) + t
    const __m256 res = _mm256_fmadd_ps(sum, scale, t);
    // save result to temp2
    _mm256_storeu_ps(&TEMP2[mid], res);
}

void process_edge() {
    for (int y = 0; y < HEIGHT; y++) {
        TEMP[idx(0, y)] = TEMP[idx(1, y)];
        TEMP[idx(HEIGHT - 1, y)] = TEMP[idx(HEIGHT - 2, y)];
    }
    for (int x = 0; x < WIDTH; x++) {
        TEMP[idx(x, 0)] = TEMP[idx(x, 1)];
        TEMP[idx(x, WIDTH - 1)] = TEMP[idx(x, WIDTH - 2)];
    }
}

void process_edge_avx() {
    for (int y = 0; y < HEIGHT; y++) {
        TEMP[idx(0, y)] = TEMP[idx(1, y)];
        TEMP[idx(HEIGHT - 1, y)] = TEMP[idx(HEIGHT - 2, y)];
    }
    for (int x = 0; x < WIDTH; x += 8) {
        __m256 second_row = _mm256_loadu_ps(&TEMP[idx(x, 1)]);
        _mm256_storeu_ps(&TEMP[idx(x, 0)], second_row);
        __m256 second_last_row = _mm256_loadu_ps(&TEMP[idx(x, WIDTH - 2)]);
        _mm256_storeu_ps(&TEMP[idx(x, WIDTH - 1)], second_last_row);
    }
}

void process_mid(int x, int y, int x_end, int y_end) {
    for (int j = y; j <= y_end; j++) {
        for (int i = x; i <= x_end; i++) {
            stencil(i, j);
        }
    }
}

void process_mid_avx(int x, int y, int x_end, int y_end) {
    for (int j = y; j <= y_end; j++) {
        for (int i = x; i < x_end; i += 8) {
            stencil_avx(i, j);
        }
    }
}

void preprocess_mid_tasks(std::vector<Task> &line_q, int i, int j) {
    Task t;
    t.i = i;
    t.j = j;
    // For inner blocks, x = [1, WIDTH - 2], y = [1, HEIGHT - 2]
    t.x = i * BLOCKSIZE + 1;
    t.x_end = minu(t.x + BLOCKSIZE - 1, WIDTH - 2);
    t.y = j * BLOCKSIZE + 1;
    t.y_end = minu(t.y + BLOCKSIZE - 1, HEIGHT - 2);
    line_q.push_back(t);
}

void step_optimized(float *temp, float *temp2, float *conduct, int width,
                    int height, int threads, int substeps) {
    if (!PROCESS_INIT) {
        threads = minu(std::thread::hardware_concurrency(), threads);
        // Block-related vars
        BLOCKSIZE = max(256, minu(height, width) / threads);
        // printf("Using threads %d, Block size = %d\n", threads, BLOCKSIZE);
        HEIGHT = height;
        WIDTH = width;
        H_COUNT = height / BLOCKSIZE;  // row
        W_COUNT = width / BLOCKSIZE;   // col
        // For stencil computation
        CONDUCT = conduct;
        TEMP = temp;
        TEMP2 = temp2;
        // Other global vars
        SUBSTEPS = substeps;

        LINE_COUNT = H_COUNT + W_COUNT - 1;
        // seperate matrix to blocks and add [i, j] to the task queue
        // zig-zag traversal of matrix (row = y axis = H_COUNT, col = x axis =
        // W_COUNT)
        for (int line = 1; line <= LINE_COUNT; line++) {
            int start_col = max(0, line - H_COUNT);
            int count = min(line, (W_COUNT - start_col), H_COUNT);
            std::vector<Task> this_line_task;
            for (int j = 0; j < count; j++) {
                int x = minu(H_COUNT, line) - j - 1;
                int y = start_col + j;
                preprocess_mid_tasks(this_line_task, x, y);
            }
            TASK_Q.push_back(this_line_task);
        }
        PROCESS_INIT = true;
    }

    ThreadPool THREAD_POOL(threads);

    for (int s = 0; s < SUBSTEPS; s++) {
        // The following tasks have dependencies:
        // Edges -> Blocks in the middle -> Swap temp and temp2
        // Process 4 edges
        THREAD_POOL.push_edge_task();
        THREAD_POOL.wait_for_all_tasks();
        // Process mid blocks
        THREAD_POOL.push_midblocks_task();
        THREAD_POOL.wait_for_all_tasks();
        // Update temp, temp2
        THREAD_POOL.push_update_task();
        THREAD_POOL.wait_for_all_tasks();
    }

    return;
}
