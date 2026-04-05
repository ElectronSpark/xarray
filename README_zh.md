# XArray — 独立基数树库

一个可移植的、独立实现的 Linux 风格基数树（XArray），适用于标准 C 环境。

XArray 将 `uint64_t` 索引映射到 `void *` 条目。每个内部节点有 64 个槽（6 位扇出）。
树会自动增长和收缩。条目可以打上最多 3 个独立标记，实现高效的过滤迭代。

## 文档

完整的公共函数、宏和类型参考，请参阅：

- **[API 参考手册 (中文)](API_zh.md)**
- **[API Reference (English)](API.md)**

## 仓库结构

```
xarray/
├── CMakeLists.txt          构建系统
├── include/
│   ├── xarray.h            公共 API
│   ├── xarray_type.h       类型定义（结构体、宏、编码）
│   └── xarray_config.h     可插拔的分配、锁和 RCU
├── src/
│   └── xarray.c            实现
└── test/
    └── test_xarray.c       cmocka 单元测试
```

## 快速开始

```sh
# 前提条件：cmake ≥ 3.10、C11 编译器、libcmocka-dev（用于测试）
mkdir build && cd build
cmake ..
make
ctest          # 或者：./test_xarray
```

## CMake 选项

| 选项              | 默认值  | 说明                                          |
|-------------------|---------|-----------------------------------------------|
| `XA_ENABLE_LOCK`  | `OFF`   | 启用内部锁编译（`XA_CONFIG_LOCK`）              |
| `XA_ENABLE_RCU`   | `OFF`   | 启用 RCU 支持编译（`XA_CONFIG_RCU`）            |
| `XA_BUILD_TESTS`  | `ON`    | 构建 cmocka 测试套件                            |
| `ENABLE_ASAN`     | `ON`    | 为测试启用 AddressSanitizer                     |

示例——启用锁编译：

```sh
cmake .. -DXA_ENABLE_LOCK=ON
```

## API 概览

### 简单 API

```c
#include "xarray.h"

struct xarray xa;
xa_init(&xa);

xa_store(&xa, 42, my_ptr, 0);           // 存储
void *p = xa_load(&xa, 42);             // 加载
void *old = xa_erase(&xa, 42);          // 擦除

xa_destroy(&xa);                        // 释放内部节点（不释放用户数据）
```

### 标记

```c
xa_set_mark(&xa, index, XA_MARK_0);     // 给条目打标记
xa_clear_mark(&xa, index, XA_MARK_0);   // 取消标记
bool marked = xa_get_mark(&xa, index, XA_MARK_0);
```

### 迭代

```c
uint64_t index;
void *entry;

// 所有条目
xa_for_each(&xa, index, entry) { ... }

// 仅带 XA_MARK_1 的条目
xa_for_each_marked(&xa, index, entry, XA_MARK_1) { ... }
```

### 值条目

小整数可以直接存储，无需堆分配：

```c
xa_store(&xa, 0, xa_mk_value(42), 0);
void *e = xa_load(&xa, 0);
if (xa_is_value(e))
    printf("%lu\n", (unsigned long)xa_to_value(e));  // 42
```

### 游标 API

用于细粒度控制（启用 `XA_CONFIG_LOCK` 时必须持有锁）：

```c
XA_STATE(xas, &xa, index);
xa_lock(&xa);
void *old = xas_store(&xas, entry);
xa_unlock(&xa);
```

---

## 移植到你的项目

### 1. 复制文件

将 `include/` 和 `src/` 目录复制到你的项目中。你只需要三个头文件和一个 `.c` 文件。

### 2. 添加到构建系统

如果使用 CMake，将库添加到项目中：

```cmake
add_library(xarray path/to/src/xarray.c)
target_include_directories(xarray PUBLIC path/to/include)
```

如果使用普通 Makefile，用 `-Ipath/to/include` 编译 `xarray.c`。

### 3. 配置功能

所有配置通过预处理器定义完成。在包含任何 xarray 头文件之前设置它们，
可以在构建系统中（`-DXA_CONFIG_LOCK`）或在项目级别的配置头文件中设置。

#### 无锁、无 RCU（默认）

无需任何操作。锁和 RCU 调用编译为空操作。适用于单线程应用或外部管理同步的场景。

#### 启用锁

定义 `XA_CONFIG_LOCK`。简单 API（`xa_store`、`xa_erase`、`xa_set_mark` 等）
将自动获取/释放锁。

默认锁实现使用 `pthread_mutex_t`。要使用自定义锁：

```c
#define XA_CONFIG_LOCK
#define XA_CUSTOM_LOCK

// 在包含 xarray 头文件之前提供以下定义：
typedef my_spinlock_t xa_lock_t;

#define XA_LOCK_INITIALIZER(name)  MY_LOCK_INIT

static inline void xa_lock_init(xa_lock_t *lock)  { my_lock_init(lock); }
static inline void xa_spin_lock(xa_lock_t *lock)   { my_lock_acquire(lock); }
static inline void xa_spin_unlock(xa_lock_t *lock) { my_lock_release(lock); }
```

#### 启用 RCU

定义 `XA_CONFIG_RCU`。读端操作（`xa_load`、`xa_find`）将被包裹在 RCU 区域内，
释放的节点将通过 `xa_call_rcu` 延迟释放。槽的加载/存储将使用 acquire/release 原子操作。

默认 RCU 桩实现会立即调用释放回调（对单线程使用是安全的）。要使用自定义 RCU：

```c
#define XA_CONFIG_RCU
#define XA_CUSTOM_RCU

// 在包含 xarray 头文件之前提供以下定义：
extern void xa_rcu_read_lock(void);
extern void xa_rcu_read_unlock(void);

typedef void (*xa_rcu_callback_t)(void *);
extern void xa_call_rcu(xa_rcu_callback_t cb, void *data);
```

### 4. 自定义内存分配

默认情况下，节点使用 `malloc` 分配、`free` 释放。
要使用自定义分配器（slab、池、arena 等）：

```c
#define XA_CUSTOM_ALLOC

// 在一个 .c 文件中实现：
void *xa_alloc_fn(size_t size)
{
    void *p = my_alloc(size);
    if (p) my_memzero(p, size);   // 必须返回已清零的内存
    return p;
}

void xa_free_fn(void *ptr)
{
    my_free(ptr);
}
```

### 5. 移植到内核/裸机环境

典型的内核移植步骤：

1. **在项目头文件中定义所有三个自定义宏**（在 xarray 头文件之前包含）：
   ```c
   #define XA_CUSTOM_ALLOC
   #define XA_CONFIG_LOCK
   #define XA_CUSTOM_LOCK
   #define XA_CONFIG_RCU
   #define XA_CUSTOM_RCU
   ```

2. **实现分配接口**，使用你的 slab/页分配器。
   `xa_alloc_fn` 必须返回至少 `size` 字节的已清零内存。

3. **实现锁接口**，使用你内核的自旋锁或互斥锁。

4. **实现 RCU 接口**，使用你内核的 RCU 子系统。关键要求：
   `xa_call_rcu(cb, data)` 必须将 `cb(data)` 延迟到所有当前 RCU 读者完成之后。

5. **替换 `<errno.h>`**（如果你的环境缺少它）。实现仅使用 `ENOMEM`（12）
   和 `EINVAL`（22）。可以手动定义：
   ```c
   #define ENOMEM 12
   #define EINVAL 22
   ```

6. **替换 `<string.h>`** — 仅使用了 `memset`（在默认 `xa_alloc_fn` 中）。
   如果定义了 `XA_CUSTOM_ALLOC`，则配置头文件不需要 `<string.h>`。

### 6. 数据结构参考

```
struct xarray          — 根。包含可选锁、标志位、头指针。
struct xa_node         — 内部节点。64 个槽，3 个标记位图。
struct xa_state        — 栈上分配的游标，用于高级 API。
```

树在存储大索引时自动增长，在节点变空时自动收缩。每层消耗索引的 6 位，
因此完整的 64 位键空间最多需要 11 层。

## 测试

测试套件使用 [cmocka](https://cmocka.org/)。安装方法：

```sh
# Debian/Ubuntu
sudo apt install libcmocka-dev

# macOS
brew install cmocka
```

测试覆盖：空/单/批量操作、顺序和随机插入、覆盖、删除不存在的键、迭代顺序、
标记设置/清除/迭代、游标 API、值条目、稀疏和大索引，以及压力测试。
所有测试默认在 AddressSanitizer 下运行。

## 许可证

派生自 Linux 内核 xarray。请参见上游许可证。
