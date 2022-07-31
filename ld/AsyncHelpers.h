#import <vector>
#import <tbb/parallel_do.h>
#import <tbb/parallel_for.h>

/*#import <Foundation/Foundation.h>

static NSOperationQueue *createQueue() {
    NSOperationQueue *queue = [[NSOperationQueue alloc] init];
    queue.qualityOfService = NSQualityOfServiceUserInteractive;
    queue.maxConcurrentOperationCount = 8;
    return queue;
}

static NSOperationQueue *sQueue = NULL;

template <typename Iterator, typename Func>
void processAsync(const Iterator &begin, const Iterator &end, Func f) {
    assert(pthread_main_np()); // Otherwise it gets weird with shared queue
    if (!sQueue) {
        sQueue = createQueue();
    }
    //printf("%s\n")
    size_t length = std::distance(begin, end);
    size_t chunkLength = MAX(1, length / sQueue.maxConcurrentOperationCount / 4);
    for (size_t i = 0; i < length; i += chunkLength) {
        [sQueue addOperationWithBlock:^{
             auto chunkEndIdx = MIN(length, i + chunkLength);
             auto chunkEnd = std::next(begin, chunkEndIdx);
            for (auto iter = std::next(begin, i); iter != chunkEnd; iter++) {
                f(*iter);
            }
        }];
    }
    [sQueue waitUntilAllOperationsAreFinished];
}*/

template <typename Iterator, typename Func>
void processAsync(const Iterator &begin, const Iterator &end, Func f) {
    tbb::parallel_do(begin, end, f);
}

template <typename Func>
void processAsyncIndexes(size_t start, size_t end, Func f) {
    std::vector<int64_t> indexes;
    indexes.reserve(end - start);
    for (size_t idx = start; idx < end; idx++) {
        indexes.emplace_back(idx);
    }
    processAsync(indexes.begin(), indexes.end(), f);
}

template <typename MappedElement, typename T, typename Func>
std::vector<MappedElement> mapAsync(const std::vector<T> &vector, Func f) {
    static_assert(std::is_pointer<MappedElement>::value, "only handles pointers rn");
    std::vector<MappedElement> elements(vector.size());
    tbb::parallel_for<size_t>(0, vector.size(), 1, [&](size_t &idx) {
        elements[idx] = f(vector[idx]);
    });
    return elements;
}
