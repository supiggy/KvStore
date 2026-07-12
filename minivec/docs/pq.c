/* ============================================================================
 * pq.c —— priority_queue 实战:前 K 个高频元素 (LeetCode 347 Top K Frequent)
 * ----------------------------------------------------------------------------
 * 这是 C++,文件名是 .c,编译要强制按 C++ 编(或改名 pq.cpp):
 *     g++ -x c++ -std=c++17 pq.c -o pq && ./pq
 *
 * 题目:给一个数组 nums 和整数 k,返回出现【频率最高】的 k 个元素。
 *   例: nums = [1,1,1,2,2,3], k=2  →  [1,2]  (1 出现3次,2 出现2次)
 *
 * 思路总览(流程树):
 *   topKFrequent(nums, k)
 *     ├─ 1. 用哈希表统计频率:  num -> 出现次数        O(N)
 *     ├─ 2. 建一个【按频率的小顶堆】,只保留 k 个
 *     │      for each (num, freq):
 *     │          heap.push(该元素)
 *     │          if heap.size() > k:  heap.pop()   ← 弹掉频率最小的(门槛)
 *     ├─ 3. 扫完后,堆里剩的 k 个就是频率最高的 k 个
 *     └─ 4. 取出它们的"数值"返回
 *   复杂度:O(N log k),空间 O(N)
 *
 * 关键点:为什么用【小顶堆】而不是大顶堆?(和 KNN topK 同一个道理)
 *   小顶堆堆顶 = 当前 k 个里频率【最小】的 = "门槛"。
 *   堆超过 k 个时,弹掉堆顶(频率最小的那个),留下的永远是频率最大的 k 个。
 *   若用大顶堆,得先把全部入堆再 pop k 次,空间 O(N)、且没法只维护 k 个。
 * ============================================================================ */

#include <iostream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <algorithm>
using namespace std;

/* ===================== 写法一:greater<pair> 小顶堆(最简洁) =====================
 * 技巧:pair 默认按 first 比较,所以把【频率放 first、数值放 second】,
 *       再用 greater<pair> 得到"按频率的小顶堆"。 */
vector<int> topKFrequent(const vector<int>& nums, int k) {
    // 1. 统计频率: num -> count
    unordered_map<int, int> freq;
    for (int x : nums) {
        freq[x]++;                 // 第一次访问 freq[x] 自动初始化为 0,再 +1
    }

    // 2. 小顶堆,元素是 pair{频率, 数值}。
    //    priority_queue 默认是大顶堆;加 greater<> 反转成小顶堆 → 频率最小在堆顶。
    priority_queue<pair<int,int>,
                   vector<pair<int,int>>,
                   greater<pair<int,int>>> heap;   // 堆顶 = 频率最小者(门槛)

    // 3. 遍历每个 (数值 num, 频率 count),维护"大小最多为 k"的小顶堆
    for (auto& kv : freq) {
        int num = kv.first, count = kv.second;
        heap.push({count, num});               // 频率放 first,才按频率比较!
        if ((int)heap.size() > k) {
            heap.pop();                        // 超过 k 个 → 弹掉频率最小的门槛
        }
    }

    // 4. 堆里剩下的 k 个就是频率最高的 k 个,取它们的 .second(数值)
    vector<int> res;
    while (!heap.empty()) {
        res.push_back(heap.top().second);
        heap.pop();
    }
    // 注意:此时 res 是按频率【从小到大】(因为是从小顶堆顶逐个弹)。
    // 想要"从高频到低频"就反转一下:
    reverse(res.begin(), res.end());
    return res;
}

/* ===================== 写法二:lambda 比较器(复习 lambda 用法) =====================
 * 这次把元素存成 {数值, 频率},用 lambda 指定"按频率(second)比较"。
 * priority_queue 用 lambda 的固定套路:模板第三参写 decltype(cmp),并把 cmp 传给构造函数。 */
vector<int> topKFrequent_lambda(const vector<int>& nums, int k) {
    unordered_map<int, int> freq;
    for (int x : nums) freq[x]++;

    // 小顶堆:频率 second 小的在堆顶。
    // 比较器返回 a.second > b.second(greater 风格)→ 小顶堆 by 频率。
    auto cmp = [](const pair<int,int>& a, const pair<int,int>& b) {
        return a.second > b.second;
    };
    priority_queue<pair<int,int>, vector<pair<int,int>>, decltype(cmp)> heap(cmp);

    for (auto& kv : freq) {// & 表示 kv 是 freq 中的一个元素,类型是 pair<const int, int>&
        //不加 & 就是把 freq 中的每个元素复制一份给 kv,类型是 pair<const int, int>
        heap.push({kv.first, kv.second});      // {数值, 频率}
        if ((int)heap.size() > k) heap.pop();  // 弹掉频率最小的
    }

    vector<int> res;
    while (!heap.empty()) {
        res.push_back(heap.top().first);       // 取数值(first)
        heap.pop();
    }
    reverse(res.begin(), res.end());           // 高频在前
    return res;
}

/* ===================== 测试 ===================== */
static void print(const char* label, const vector<int>& v) {
    cout << label;
    for (int x : v) cout << x << " ";
    cout << "\n";
}

int main() {
    vector<int> nums = {1, 1, 1, 2, 2, 3};
    int k = 2;

    print("写法一 (greater<pair>) 前2高频: ", topKFrequent(nums, k));        // 期望 1 2
    print("写法二 (lambda)        前2高频: ", topKFrequent_lambda(nums, k)); // 期望 1 2

    vector<int> nums2 = {4, 4, 4, 6, 6, 6, 6, 7, 7, 5};
    // 频率: 6->4, 4->3, 7->2, 5->1  → 前3高频 = 6,4,7
    print("第二组 前3高频:                ", topKFrequent(nums2, 3));         // 期望 6 4 7

    return 0;
}
