#include <iostream>
#include <vector>
#include <functional>
#include <stdexcept>
using namespace std;

class Heap {
private:
    // 用数组存储堆。
    // 堆逻辑上是一棵完全二叉树，但实际不需要建树，用数组即可。
    vector<int> data;

    // 比较函数。
    // cmp(a, b) == true 表示 a 的优先级比 b 高，a 应该排在 b 上面。
    //
    // 小顶堆：cmp(a, b) = a < b
    // 含义：数值更小的元素优先级更高。
    //
    // 大顶堆：cmp(a, b) = a > b
    // 含义：数值更大的元素优先级更高。
    function<bool(int, int)> cmp;

public:
    // 构造函数，传入比较规则。
    Heap(function<bool(int, int)> cmp) : cmp(cmp) {}

    // 插入一个元素。
    //
    // 步骤：
    // 1. 先把新元素放到数组末尾，也就是完全二叉树的最后一个位置。
    // 2. 然后向上调整 siftUp，让它回到正确位置。
    //
    // 时间复杂度：O(log n)
    void push(int x) {
        data.push_back(x);
        siftUp(data.size() - 1);
    }

    // 弹出堆顶元素。
    //
    // 堆顶：
    // - 小顶堆中是最小值
    // - 大顶堆中是最大值
    //
    // 步骤：
    // 1. 记录当前堆顶 data[0]。
    // 2. 用最后一个元素覆盖堆顶。
    // 3. 删除最后一个元素。
    // 4. 从堆顶开始向下调整 siftDown，恢复堆性质。
    //
    // 时间复杂度：O(log n)
    int pop() {
        if (data.empty()) {
            throw runtime_error("heap is empty");
        }

        int topValue = data[0];

        data[0] = data.back();
        data.pop_back();

        if (!data.empty()) {
            siftDown(0);
        }

        return topValue;
    }

    // 查看堆顶元素，但不删除。
    //
    // 时间复杂度：O(1)
    int top() const {
        if (data.empty()) {
            throw runtime_error("heap is empty");
        }

        return data[0];
    }

    // 判断堆是否为空。
    bool empty() const {
        return data.empty();
    }

    // 返回堆中元素个数。
    int size() const {
        return data.size();
    }

private:
    // 向上调整。
    //
    // 使用场景：
    // 插入新元素后，新元素位于数组末尾，可能比父节点优先级更高。
    // 所以需要不断和父节点比较，如果优先级更高，就和父节点交换。
    //
    // 数组下标关系：
    // 当前节点下标：i
    // 父节点下标：(i - 1) / 2
    void siftUp(int i) {
        while (i > 0) {
            int parent = (i - 1) / 2;

            // 如果当前节点比父节点优先级更高，就交换。
            // 小顶堆中，当前节点更小就上浮。
            // 大顶堆中，当前节点更大就上浮。
            if (cmp(data[i], data[parent])) {
                swap(data[i], data[parent]);
                i = parent;
            } else {
                // 如果当前节点已经不比父节点优先级高，
                // 说明堆性质已经满足，可以停止。
                break;
            }
        }
    }

    // 向下调整。
    //
    // 使用场景：
    // 弹出堆顶后，用最后一个元素补到堆顶。
    // 这个元素可能比子节点优先级低，所以需要不断向下交换。
    //
    // 数组下标关系：
    // 当前节点下标：i
    // 左孩子下标：2 * i + 1
    // 右孩子下标：2 * i + 2
    void siftDown(int i) {
        int n = data.size();

        while (true) {
            int left = 2 * i + 1;
            int right = 2 * i + 2;

            // best 表示当前节点、左孩子、右孩子中优先级最高的那个位置。
            int best = i;

            // 如果左孩子存在，并且左孩子优先级比 best 更高，更新 best。
            if (left < n && cmp(data[left], data[best])) {
                best = left;
            }

            // 如果右孩子存在，并且右孩子优先级比 best 更高，更新 best。
            if (right < n && cmp(data[right], data[best])) {
                best = right;
            }

            // 如果 best 还是当前节点，说明当前节点已经比两个孩子都更优先，
            // 堆性质满足，停止调整。
            if (best == i) {
                break;
            }

            // 否则，把当前节点和优先级更高的孩子交换。
            swap(data[i], data[best]);

            // 继续从被交换下去的位置向下调整。
            i = best;
        }
    }
};