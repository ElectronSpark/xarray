# XArray API 参考手册

XArray 库中所有公开函数、宏和类型的详细参考文档。

---

## 目录

- [类型与常量](#类型与常量)
- [条目编码](#条目编码)
- [初始化与销毁](#初始化与销毁)
- [简单 API](#简单-api)
  - [xa_load](#xa_load)
  - [xa_store](#xa_store)
  - [xa_erase](#xa_erase)
  - [xa_empty](#xa_empty)
  - [xa_destroy](#xa_destroy)
- [标记 API](#标记-api)
  - [xa_set_mark](#xa_set_mark)
  - [xa_clear_mark](#xa_clear_mark)
  - [xa_get_mark](#xa_get_mark)
- [搜索 API](#搜索-api)
  - [xa_find](#xa_find)
  - [xa_find_after](#xa_find_after)
- [迭代宏](#迭代宏)
  - [xa_for_each](#xa_for_each)
  - [xa_for_each_marked](#xa_for_each_marked)
- [游标（高级）API](#游标高级api)
  - [XA_STATE](#xa_state)
  - [xas_set](#xas_set)
  - [xas_rewind](#xas_rewind)
  - [xas_set_err](#xas_set_err)
  - [xas_error](#xas_error)
  - [xas_retry](#xas_retry)
  - [xas_load](#xas_load)
  - [xas_store](#xas_store)
  - [xas_find](#xas_find)
  - [xas_find_marked](#xas_find_marked)
  - [xas_set_mark](#xas_set_mark)
  - [xas_clear_mark](#xas_clear_mark)
  - [xas_get_mark](#xas_get_mark)
- [锁辅助函数](#锁辅助函数)
  - [xa_lock / xa_unlock](#xa_lock--xa_unlock)
  - [xa_rcu_lock / xa_rcu_unlock](#xa_rcu_lock--xa_rcu_unlock)
- [值条目](#值条目)
  - [xa_mk_value](#xa_mk_value)
  - [xa_to_value](#xa_to_value)
  - [xa_is_value](#xa_is_value)
- [构建配置](#构建配置)

---

## 类型与常量

### `struct xarray`

XArray 的根结构体。包含树的头指针、用于标记管理的标志位，以及一个可选的锁。

```c
struct xarray {
#ifdef XA_CONFIG_LOCK
    xa_lock_t   xa_lock;
#endif
    unsigned int xa_flags;
    void        *xa_head;
};
```

用户应将此结构体视为不透明的，仅通过 API 函数与之交互。

### `struct xa_state`

高级 API 使用的游标结构体。保存当前在树中的遍历位置。
使用 `XA_STATE()` 在栈上实例化。

### `xa_mark_t`

标识标记的无符号整数。提供三个标记：

| 常量         | 值    | 说明            |
|-------------|-------|-----------------|
| `XA_MARK_0` | 0     | 第一个标记位图    |
| `XA_MARK_1` | 1     | 第二个标记位图    |
| `XA_MARK_2` | 2     | 第三个标记位图    |
| `XA_MARK_MAX` | 0xFF | 哨兵值：匹配任何条目（`xa_for_each` 内部使用） |

`XA_MAX_MARKS` 定义为 `3`。所有标记函数会静默忽略 ≥ 3 的标记值。

### 树常量

| 常量               | 值  | 说明                         |
|--------------------|-----|------------------------------|
| `XA_CHUNK_SHIFT`   | 6   | 每层树消耗的位数               |
| `XA_CHUNK_SIZE`    | 64  | 每个内部节点的槽数（2^6）       |
| `XA_CHUNK_MASK`    | 63  | 用于提取槽偏移的掩码            |
| `XA_MAX_MARKS`     | 3   | 独立标记位图的数量              |

树支持完整的 64 位键空间，最多 ⌈64/6⌉ = 11 层。

### 哨兵值

| 常量               | 说明                                                     |
|--------------------|----------------------------------------------------------|
| `XA_ZERO_ENTRY`    | `xa_store` 出错时返回。也用作内部占位符。                     |
| `XA_RETRY_ENTRY`   | 表示槽正在被修改（RCU 读者应重试）。                          |

---

## 条目编码

XArray 可以存储两种用户条目：

1. **指针条目** — 任何对齐的 `void *` 指针（低 2 位必须为 0）。
2. **值条目** — 编码为标记指针的小整数。参见[值条目](#值条目)。

**限制**：不能存储 `NULL`、`XA_ZERO_ENTRY`、`XA_RETRY_ENTRY`，或任何
`xa_is_internal()` 返回 true 的值。存储 `NULL` 等同于擦除。存储内部条目会被
`xa_store` 拒绝（返回 `XA_ZERO_ENTRY`）。

---

## 初始化与销毁

### `xa_init`

```c
void xa_init(struct xarray *xa);
```

使用默认设置（无标志位）初始化 xarray。必须在对 xarray 执行任何其他操作之前调用。

**参数：**
- `xa` — 要初始化的 xarray 指针。

**配合使用：** 本库中的所有其他 API 都要求事先调用 `xa_init`（或 `xa_init_flags`）。
当 xarray 不再需要时，需配合清理序列：使用 `xa_for_each` 迭代释放用户条目，然后调用
`xa_destroy` 释放内部节点。

```c
/* 完整生命周期 */
struct xarray xa;
xa_init(&xa);                           // 1. 初始化

xa_store(&xa, 0, my_ptr, 0);            // 2. 使用数组
xa_set_mark(&xa, 0, XA_MARK_0);

uint64_t idx;
void *entry;
xa_for_each(&xa, idx, entry) {          // 3. 清空用户条目
    xa_erase(&xa, idx);
    free(entry);
}
xa_destroy(&xa);                        // 4. 释放内部节点
```

### `xa_init_flags`

```c
void xa_init_flags(struct xarray *xa, unsigned int flags);
```

使用调用者指定的标志位初始化 xarray。标志位字段保留用于内部记录（头部级别的标记位）；
用户提供的标志位占据低位。

**参数：**
- `xa` — 要初始化的 xarray 指针。
- `flags` — 初始标志位值。

---

## 简单 API

简单 API 在启用 `XA_CONFIG_LOCK` 时会自动处理加锁。这些函数可以在任何上下文中安全调用。

### `xa_load`

```c
void *xa_load(struct xarray *xa, uint64_t index);
```

查找存储在 `index` 处的条目。

**参数：**
- `xa` — 要搜索的 xarray。
- `index` — 要查找的索引（0 到 `UINT64_MAX`）。

**返回值：**
- 存储在 `index` 处的条目，如果不存在则返回 `NULL`。
- 内部条目会被过滤为 `NULL`。

**加锁：**
- 如果启用了 `XA_CONFIG_RCU`，内部会获取 `xa_rcu_lock` / `xa_rcu_unlock`。
- 无需写锁。

**配合使用：** `xa_load` 是 `xa_store` 的读取对应函数。典型模式是先存储条目，
之后通过索引检索它们。当条目可能是值条目（通过 `xa_mk_value` 创建）时，
在解释结果前用 `xa_is_value` 进行检测。

```c
xa_store(&xa, 5, xa_mk_value(100), 0);  // 存储值条目

void *e = xa_load(&xa, 5);              // 检索
if (xa_is_value(e))
    printf("value = %lu\n", xa_to_value(e));
```

---

### `xa_store`

```c
void *xa_store(struct xarray *xa, uint64_t index, void *entry, uint64_t gfp);
```

将 `entry` 存储在 `index` 处。如果该索引已有条目，则替换并返回旧条目。
覆盖时条目上的标记会被保留，擦除时清除。

存储 `NULL` 等同于调用 `xa_erase`。

**参数：**
- `xa` — 要修改的 xarray。
- `index` — 目标索引（0 到 `UINT64_MAX`）。
- `entry` — 要存储的条目。不能是内部条目（`xa_is_internal(entry)` 必须为 false）。`NULL` 表示擦除。
- `gfp` — 保留供将来使用（传 `0`）。

**返回值：**
- `index` 处的旧条目，如果槽位为空则返回 `NULL`。
- 出错时返回 `XA_ZERO_ENTRY`（分配失败或无效条目）。xarray 保持不变。

**加锁：**
- 如果启用了 `XA_CONFIG_LOCK`，内部会获取 `xa_lock` / `xa_unlock`。

**标记行为：**
- 覆盖已有条目会保留其上的所有标记。
- 存储 `NULL`（擦除）会清除所有标记。
- 存储到空槽位时无标记。

**配合使用：** `xa_store` 通常与 `xa_load` 配合用于检索，与 `xa_erase` 配合用于
删除。存储后可以用 `xa_set_mark` 给条目打标记，然后用 `xa_for_each_marked`
仅迭代打了标记的条目。存储-标记-迭代三件套是最常见的多 API 工作流：

```c
/* 存储 → 标记 → 迭代 三件套 */
xa_store(&xa, 10, page_a, 0);
xa_store(&xa, 20, page_b, 0);

xa_set_mark(&xa, 10, XA_MARK_0);        // 标记为脏
xa_set_mark(&xa, 20, XA_MARK_0);

uint64_t idx;
void *entry;
xa_for_each_marked(&xa, idx, entry, XA_MARK_0) {
    flush(entry);                       // 回写脏页
    xa_clear_mark(&xa, idx, XA_MARK_0); // 清除脏标记
}
```

检查返回值：`XA_ZERO_ENTRY` 表示出错，任何非 NULL 的非错误返回值是调用者
可能需要释放的旧条目。

---

### `xa_erase`

```c
void *xa_erase(struct xarray *xa, uint64_t index);
```

移除 `index` 处的条目。当内部节点变空时，树会自动收缩。

**参数：**
- `xa` — 要修改的 xarray。
- `index` — 要擦除的索引。

**返回值：**
- 被移除的条目，如果 `index` 处不存在条目则返回 `NULL`。

**加锁：**
- 如果启用了 `XA_CONFIG_LOCK`，内部会获取 `xa_lock` / `xa_unlock`。

**配合使用：** `xa_erase` 是 `xa_store` 的逆操作。用于移除单个条目。
返回的指针由调用者负责释放。在批量删除场景中，将 `xa_for_each` 与 `xa_erase`
组合使用，最后调用 `xa_destroy`：

```c
uint64_t idx;
void *entry;
xa_for_each(&xa, idx, entry) {
    xa_erase(&xa, idx);
    free(entry);
}
xa_destroy(&xa);  // 释放内部节点
```

---

### `xa_empty`

```c
bool xa_empty(const struct xarray *xa);
```

检查 xarray 是否包含任何条目。

**参数：**
- `xa` — 要测试的 xarray。

**返回值：**
- 如果 xarray 没有条目则返回 `true`，否则返回 `false`。

**加锁：**
- 无锁。启用 `XA_CONFIG_RCU` 时使用 volatile 加载。

**配合使用：** `xa_empty` 通常用作迭代前的保护条件，或批量擦除后的后置条件检查：

```c
if (!xa_empty(&xa)) {
    uint64_t idx;
    void *entry;
    xa_for_each(&xa, idx, entry)
        process(entry);
}
```

---

### `xa_destroy`

```c
void xa_destroy(struct xarray *xa);
```

释放 xarray 的所有内部节点。调用后 xarray 变为空。

**重要：** 此函数**不会**释放用户条目。调用者必须在调用 `xa_destroy` 之前
释放或分离所有已存储的条目，否则这些条目会被泄漏。

在已经为空的 xarray 上调用 `xa_destroy` 是安全的（空操作）。多次调用也是安全的。

**参数：**
- `xa` — 要销毁的 xarray。

**加锁：**
- 如果启用了 `XA_CONFIG_LOCK`，内部会获取 `xa_lock` / `xa_unlock`。

**配合使用：** `xa_destroy` 应该始终是 xarray 生命周期中的最后一个调用。
在调用之前，用 `xa_for_each` + `xa_erase` 清空所有用户条目（如果需要释放的话）。
`xa_destroy` 之后，xarray 为空，可以用 `xa_init` 重新使用。

```c
/* 安全销毁模式 */
uint64_t index;
void *entry;
xa_for_each(&xa, index, entry) {  // 1. 释放用户条目
    xa_erase(&xa, index);
    free(entry);
}
xa_destroy(&xa);                  // 2. 释放内部节点
```

---

## 标记 API

每个条目可以独立地打上最多 3 个标记（`XA_MARK_0`、`XA_MARK_1`、`XA_MARK_2`）。
标记会沿树向上传播，使得标记迭代高效——只有包含标记条目的子树才会被访问。

标记行为总结：

| 操作                  | 对标记的影响                  |
|-----------------------|------------------------------|
| `xa_store`（新条目）   | 无标记                       |
| `xa_store`（覆盖）     | 保留所有已有标记              |
| `xa_erase`            | 清除所有标记                  |
| `xa_store(NULL)`      | 同擦除——清除所有标记          |

### `xa_set_mark`

```c
void xa_set_mark(struct xarray *xa, uint64_t index, xa_mark_t mark);
```

在 `index` 处的条目上设置 `mark`。如果 `index` 处没有条目或 `mark` ≥ `XA_MAX_MARKS`，
则不执行任何操作。

**参数：**
- `xa` — xarray。
- `index` — 要标记的条目索引。
- `mark` — 要设置的标记（`XA_MARK_0`、`XA_MARK_1` 或 `XA_MARK_2`）。

**加锁：**
- 内部获取 `xa_lock` / `xa_unlock`。

**配合使用：** `xa_set_mark` 通常在 `xa_store` 之后使用（条目必须存在才能被标记），
然后配合 `xa_for_each_marked` 仅迭代标记的条目，或配合 `xa_find` / `xa_find_after`
按标记搜索。处理后调用 `xa_clear_mark` 移除标记：

```c
xa_store(&xa, 7, ptr, 0);               // 存储条目
xa_set_mark(&xa, 7, XA_MARK_1);         // 打标记

if (xa_get_mark(&xa, 7, XA_MARK_1))     // 查询标记
    printf("已标记!\n");

xa_clear_mark(&xa, 7, XA_MARK_1);       // 移除标记
```

---

### `xa_clear_mark`

```c
void xa_clear_mark(struct xarray *xa, uint64_t index, xa_mark_t mark);
```

清除 `index` 处条目上的 `mark`。如果 `index` 处没有条目或 `mark` ≥ `XA_MAX_MARKS`，
则不执行任何操作。清除未设置的标记是安全的空操作。

**参数：**
- `xa` — xarray。
- `index` — 要取消标记的条目索引。
- `mark` — 要清除的标记。

**加锁：**
- 内部获取 `xa_lock` / `xa_unlock`。

**配合使用：** `xa_clear_mark` 是 `xa_set_mark` 的对应操作。
最常见的模式是在 `xa_for_each_marked` 中设置-处理-清除：

```c
uint64_t idx;
void *entry;
xa_for_each_marked(&xa, idx, entry, XA_MARK_0) {
    write_back(entry);                   // 处理脏条目
    xa_clear_mark(&xa, idx, XA_MARK_0);  // 标记为干净
}
```

---

### `xa_get_mark`

```c
bool xa_get_mark(struct xarray *xa, uint64_t index, xa_mark_t mark);
```

测试 `index` 处的条目是否设置了 `mark`。

**参数：**
- `xa` — xarray。
- `index` — 要测试的索引。
- `mark` — 要测试的标记。

**返回值：**
- 如果条目存在且设置了 `mark`，返回 `true`，否则返回 `false`。
- 对超出范围的标记（≥ `XA_MAX_MARKS`）返回 `false`。
- 对空索引返回 `false`。

**加锁：**
- 内部获取 `xa_rcu_lock` / `xa_rcu_unlock`。

**配合使用：** `xa_get_mark` 是单点查询——用于测试单个已知索引上的标记。
对于批量查询，改用 `xa_for_each_marked` 或带标记过滤的 `xa_find`。

---

## 搜索 API

### `xa_find`

```c
void *xa_find(struct xarray *xa, uint64_t *indexp, uint64_t max, xa_mark_t mark);
```

查找 `*indexp` 处或之后的第一个条目，上限为 `max`。

当 `mark` 为 `XA_MARK_MAX` 时，匹配任何条目（遍历所有条目）。
当 `mark` 为特定标记值时，只匹配带有该标记的条目。

**参数：**
- `xa` — 要搜索的 xarray。
- `indexp` — 指向起始索引的指针。成功时更新为找到的条目的索引。
- `max` — 搜索的最大索引（含）。
- `mark` — `XA_MARK_MAX` 表示所有条目，或特定标记进行过滤。

**返回值：**
- 第一个匹配的条目，如果在 `[*indexp, max]` 范围内未找到则返回 `NULL`。

**加锁：**
- 内部获取 `xa_rcu_lock` / `xa_rcu_unlock`。

**配合使用：** `xa_find` 通常与 `xa_find_after` 配对使用来遍历条目。
用 `xa_find` 获取第一个结果，然后用 `xa_find_after` 循环。
它们一起实现了与 `xa_for_each` 相同的遍历，但允许调用者控制步进：

```c
uint64_t idx = 0;
void *entry = xa_find(&xa, &idx, UINT64_MAX, XA_MARK_MAX);
while (entry) {
    printf("[%lu] = %p\n", (unsigned long)idx, entry);
    entry = xa_find_after(&xa, &idx, UINT64_MAX, XA_MARK_MAX);
}
```

要仅搜索标记的条目，传入特定标记代替 `XA_MARK_MAX`——这正是 `xa_for_each_marked`
内部所做的。

---

### `xa_find_after`

```c
void *xa_find_after(struct xarray *xa, uint64_t *indexp, uint64_t max,
                    xa_mark_t mark);
```

查找 `*indexp` 之后的下一个条目，上限为 `max`。等同于递增 `*indexp` 后调用 `xa_find`。

**参数：**
- `xa` — xarray。
- `indexp` — 指向当前索引的指针。搜索从 `*indexp + 1` 开始。成功时更新。
- `max` — 最大索引（含）。
- `mark` — `XA_MARK_MAX` 或特定标记。

**返回值：**
- 下一个匹配的条目，如果未找到则返回 `NULL`。
- 如果 `*indexp >= max` 则立即返回 `NULL`。

**配合使用：** `xa_find_after` 是 `xa_find` 的后续操作。参见上面 `xa_find`
中的循环示例。当你需要中断迭代、应用过滤器或将结果收集到另一个容器时，
这对函数可以替代 `xa_for_each`。

---

## 迭代宏

### `xa_for_each`

```c
xa_for_each(xa, index, entry) { ... }
```

按索引升序迭代 xarray 中的每个条目。

**参数：**
- `xa` — `struct xarray` 的指针。
- `index` — `uint64_t` 变量，每次迭代时设置为当前条目的索引。
- `entry` — `void *` 变量，设置为当前条目。

只产生非 NULL、非内部的条目。

**配合使用：** `xa_for_each` 内部基于 `xa_find` + `xa_find_after` 实现。
它是扫描整个数组的主要方式。可与 `xa_erase` 组合用于批量删除，或与 `xa_set_mark`
组合用于在全量扫描中给条目打标记：

```c
/* 扫描所有条目，标记需要处理的 */
uint64_t index;
void *entry;
xa_for_each(&xa, index, entry) {
    if (needs_flush(entry))
        xa_set_mark(&xa, index, XA_MARK_0);
}

/* 然后只处理标记的子集 */
xa_for_each_marked(&xa, index, entry, XA_MARK_0) {
    flush(entry);
    xa_clear_mark(&xa, index, XA_MARK_0);
}
```

---

### `xa_for_each_marked`

```c
xa_for_each_marked(xa, index, entry, mark) { ... }
```

按索引升序迭代设置了 `mark` 的条目。

**参数：**
- `xa` — `struct xarray` 的指针。
- `index` — `uint64_t` 循环变量。
- `entry` — `void *` 循环变量。
- `mark` — 要过滤的标记（`XA_MARK_0`、`XA_MARK_1` 或 `XA_MARK_2`）。

**配合使用：** `xa_for_each_marked` 是标记工作流的读取端。完整周期为：

1. `xa_store` — 添加条目。
2. `xa_set_mark` — 给需要关注的条目打标记。
3. `xa_for_each_marked` — 仅迭代打了标记的条目。
4. `xa_clear_mark` — 处理后取消标记。

```c
uint64_t index;
void *entry;
xa_for_each_marked(&xa, index, entry, XA_MARK_0) {
    process_dirty(index, entry);
    xa_clear_mark(&xa, index, XA_MARK_0);
}
```

---

## 游标（高级）API

游标 API 提供对树的直接访问，对批量操作更高效。启用 `XA_CONFIG_LOCK` 时，
调用者必须为写操作（`xas_store`、`xas_set_mark`、`xas_clear_mark`）持有 `xa_lock`，
为读操作（`xas_load`、`xas_find`、`xas_get_mark`）持有 `xa_rcu_lock`。

### `XA_STATE`

```c
XA_STATE(name, array, index);
```

在栈上声明并初始化一个游标（`struct xa_state`）。

**参数：**
- `name` — 游标的变量名。
- `array` — `struct xarray` 的指针。
- `index` — 游标的初始索引。

**配合使用：** `XA_STATE` 是每个游标操作的入口。声明游标后，典型的写入工作流为：

```c
/* 游标写入工作流 */
xa_lock(&xa);
XA_STATE(xas, &xa, index);
void *old = xas_store(&xas, new_entry);  // 在锁内存储
if (xas_error(&xas))
    handle_error(xas_error(&xas));
xa_unlock(&xa);
```

读取工作流为：

```c
/* 游标读取工作流 */
xa_rcu_lock();
XA_STATE(xas, &xa, index);
void *entry = xas_load(&xas);
if (entry && !xas_retry(&xas, entry))
    use(entry);
xa_rcu_unlock();
```

---

### `xas_set`

```c
void xas_set(struct xa_state *xas, uint64_t index);
```

将游标重置到新索引。清除任何错误状态，并将游标标记为需要重启
（下次操作时会重新遍历树）。

**参数：**
- `xas` — 要重置的游标。
- `index` — 新索引。

**配合使用：** `xas_set` 用于在 `xas_store` 报告错误后重新定位游标，
或在单次加锁区间内遍历多个索引：

```c
xa_lock(&xa);
XA_STATE(xas, &xa, 0);
xas_store(&xas, entry_a, 0);

xas_set(&xas, 100);              // 重新定位到索引 100
xas_store(&xas, entry_b, 0);     // 在索引 100 存储
xa_unlock(&xa);
```

---

### `xas_rewind`

```c
void xas_rewind(struct xa_state *xas);
```

重置游标，在不改变索引的情况下从当前索引重新开始遍历。

---

### `xas_set_err`

```c
void xas_set_err(struct xa_state *xas, int err);
```

将游标置于错误状态。后续操作（`xas_find`、`xas_find_marked`、`xas_get_mark`）
将返回 `NULL` / `false`，直到通过 `xas_set` 清除错误。

**参数：**
- `xas` — 游标。
- `err` — 错误码（通常为 `-ENOMEM` 或 `-EINVAL`）。

---

### `xas_error`

```c
int xas_error(const struct xa_state *xas);
```

获取游标中的错误码。

**返回值：**
- 如果游标处于错误状态则返回错误码，否则返回 `0`。

**配合使用：** 在 `xas_store` 之后务必检查 `xas_error`。如果非零，
则存储失败（通常是 `-ENOMEM`）。使用 `xas_set` 清除错误并重试：

```c
xa_lock(&xa);
XA_STATE(xas, &xa, idx);
xas_store(&xas, entry);
if (xas_error(&xas)) {
    int err = xas_error(&xas);
    xa_unlock(&xa);
    // 处理错误，例如释放内存后重试
    return err;
}
xa_unlock(&xa);
```

---

### `xas_retry`

```c
bool xas_retry(struct xa_state *xas, const void *entry);
```

检查 `entry` 是否为 `XA_RETRY_ENTRY`，如果是则回退游标。用于 RCU 读端循环中
处理并发修改。

**返回值：**
- 如果需要重试（游标已回退）则返回 `true`，否则返回 `false`。

**配合使用：** `xas_retry` 在 RCU 读端循环中与 `xas_load` 配合使用。
当并发写者替换了节点时，读者会看到 `XA_RETRY_ENTRY`。调用 `xas_retry`
来检测并重新遍历：

```c
xa_rcu_lock();
XA_STATE(xas, &xa, idx);
void *entry;
do {
    entry = xas_load(&xas);
} while (xas_retry(&xas, entry));
// entry 现在是稳定的
xa_rcu_unlock();
```

---

### `xas_load`

```c
void *xas_load(struct xa_state *xas);
```

遍历树并加载游标当前索引处的条目。返回后，游标定位在包含该条目的节点上。

**返回值：**
- `xas->xa_index` 处的条目，如果不存在则返回 `NULL`。

**加锁：**
- 调用者必须持有 `xa_rcu_lock`（如果启用了 RCU）。

**配合使用：** `xas_load` 后通常跟随条件 `xas_store`（加载后修改模式），
或跟随 `xas_set_mark` / `xas_get_mark` 在游标定位后查询/更新标记：

```c
/* 在锁内加载后修改 */
xa_lock(&xa);
XA_STATE(xas, &xa, idx);
void *old = xas_load(&xas);
if (old && should_replace(old)) {
    xas_store(&xas, new_entry);
    free(old);
}
xa_unlock(&xa);
```

---

### `xas_store`

```c
void *xas_store(struct xa_state *xas, void *entry);
```

在游标当前索引处存储 `entry`。根据需要创建中间节点。
存储 `NULL` 会擦除条目并可能触发树收缩。

游标的 `xa_sibs` 字段控制多槽（兄弟）条目：如果 `xa_sibs` 非零，
从游标偏移开始的 `xa_sibs + 1` 个连续槽位将被该条目占据。

**参数：**
- `xas` — 游标（必须通过 `XA_STATE` 或 `xas_set` 定位）。
- `entry` — 要存储的条目，或 `NULL` 表示擦除。

**返回值：**
- 该索引处的旧条目，或 `NULL`。
- 分配失败时设置 `xas_error` 为 `-ENOMEM`。
- 兄弟跨度无效时设置 `xas_error` 为 `-EINVAL`。

**加锁：**
- 调用者必须持有 `xa_lock`。

**标记行为：**
- 覆盖时保留规范槽上的已有标记。
- 擦除时清除所有标记。

**配合使用：** `xas_store` 是底层写入原语。对于批量存储，声明一个 `XA_STATE`，
获取一次 `xa_lock`，然后在循环中调用 `xas_set` + `xas_store` 以分摊锁开销：

```c
xa_lock(&xa);
XA_STATE(xas, &xa, 0);
for (int i = 0; i < N; i++) {
    xas_set(&xas, keys[i]);
    xas_store(&xas, values[i]);
    if (xas_error(&xas))
        break;
}
xa_unlock(&xa);
```

对于兄弟（多槽）条目，在调用 `xas_store` 前设置 `xas.xa_sibs`：

```c
xa_lock(&xa);
XA_STATE(xas, &xa, base_index);
xas.xa_sibs = 3;                // 占据 4 个连续槽位
xas_store(&xas, huge_page);
xa_unlock(&xa);
```

---

### `xas_find`

```c
void *xas_find(struct xa_state *xas, uint64_t max);
```

在游标当前位置之后查找下一个非 NULL 条目，上限为 `max`。
首次调用（在 `XA_STATE` 或 `xas_set` 之后）从游标索引开始。
后续调用会跳过上一个结果继续前进。

**参数：**
- `xas` — 游标。
- `max` — 最大索引（含）。

**返回值：**
- 下一个条目，如果未找到则返回 `NULL`。
- `xas->xa_index` 更新为找到的条目的索引。
- 如果游标处于错误状态则返回 `NULL`。

**加锁：**
- 调用者必须持有 `xa_rcu_lock`（如果启用了 RCU）。

**配合使用：** `xas_find` 是 `xa_find_after` 的游标级等价物。
在循环中使用它可以在不释放锁的情况下逐步遍历条目：

```c
/* 在一次加锁中收集 [0, 1000] 范围内的所有条目 */
xa_rcu_lock();
XA_STATE(xas, &xa, 0);
void *entry;
while ((entry = xas_find(&xas, 1000)) != NULL) {
    collect(xas.xa_index, entry);
}
xa_rcu_unlock();
```

---

### `xas_find_marked`

```c
void *xas_find_marked(struct xa_state *xas, uint64_t max, xa_mark_t mark);
```

在游标当前位置之后查找下一个设置了 `mark` 的条目。

**参数：**
- `xas` — 游标。
- `max` — 最大索引（含）。
- `mark` — 要搜索的标记（`XA_MARK_0`、`XA_MARK_1` 或 `XA_MARK_2`）。

**返回值：**
- 下一个带标记的条目，或 `NULL`。
- 对无效标记（≥ `XA_MAX_MARKS`）或错误状态返回 `NULL`。

**加锁：**
- 调用者必须持有 `xa_rcu_lock`（如果启用了 RCU）。

**配合使用：** `xas_find_marked` 是 `xa_for_each_marked` 的游标对应物。
与 `xas_clear_mark` 组合可在一次加锁遍历中处理和取消标记条目：

```c
xa_lock(&xa);
XA_STATE(xas, &xa, 0);
void *entry;
while ((entry = xas_find_marked(&xas, UINT64_MAX, XA_MARK_0)) != NULL) {
    flush(entry);
    xas_clear_mark(&xas, XA_MARK_0);
}
xa_unlock(&xa);
```

---

### `xas_set_mark`

```c
void xas_set_mark(struct xa_state *xas, xa_mark_t mark);
```

在游标当前位置的条目上设置 `mark`。游标必须已通过先前的 `xas_load` 或
`xas_store` 定位。标记会向上传播到所有祖先节点。

**参数：**
- `xas` — 游标。
- `mark` — 要设置的标记。

如果游标处于错误/特殊状态或 `mark` ≥ `XA_MAX_MARKS`，则不执行任何操作。

**加锁：**
- 调用者必须持有 `xa_lock`。

**配合使用：** `xas_set_mark` 要求游标先定位——在标记前调用 `xas_load` 或
`xas_store`。典型模式是加载后标记：

```c
xa_lock(&xa);
XA_STATE(xas, &xa, idx);
void *entry = xas_load(&xas);    // 定位游标
if (entry)
    xas_set_mark(&xas, XA_MARK_1);  // 打标记
xa_unlock(&xa);
```

---

### `xas_clear_mark`

```c
void xas_clear_mark(struct xa_state *xas, xa_mark_t mark);
```

清除游标当前位置条目上的 `mark`。如果同一节点中没有其他槽设置了该标记，
标记也会从祖先节点中清除。

**参数：**
- `xas` — 游标。
- `mark` — 要清除的标记。

**加锁：**
- 调用者必须持有 `xa_lock`。

**配合使用：** `xas_clear_mark` 是 `xas_set_mark` 的对应操作。
参见上面 `xas_find_marked` 中的查找-处理-清除循环示例。

---

### `xas_get_mark`

```c
bool xas_get_mark(struct xa_state *xas, xa_mark_t mark);
```

测试游标当前位置是否设置了 `mark`。

**返回值：**
- 如果标记已设置则返回 `true`，否则返回 `false`。
- 对错误/特殊游标状态或无效标记返回 `false`。

**加锁：**
- 调用者必须持有 `xa_rcu_lock`（如果启用了 RCU）。

**配合使用：** `xas_get_mark` 是标记的游标级读取操作。
与 `xas_load` 组合可在定位的游标上条件检查标记而不修改它们：

```c
xa_rcu_lock();
XA_STATE(xas, &xa, idx);
void *entry = xas_load(&xas);
if (entry && xas_get_mark(&xas, XA_MARK_2))
    printf("索引 %lu 处的条目有 MARK_2\n", (unsigned long)idx);
xa_rcu_unlock();
```

---

## 锁辅助函数

### `xa_lock` / `xa_unlock`

```c
void xa_lock(struct xarray *xa);
void xa_unlock(struct xarray *xa);
```

获取/释放 xarray 的内部锁。使用游标 API 进行写操作时必须调用。

未定义 `XA_CONFIG_LOCK` 时编译为空操作。

**配合使用：** `xa_lock` / `xa_unlock` 包围所有游标写操作：`xas_store`、
`xas_set_mark`、`xas_clear_mark`。简单 API（`xa_store`、`xa_erase` 等）
会自动获取锁，因此只有使用游标 API 时才需要显式加锁。

---

### `xa_rcu_lock` / `xa_rcu_unlock`

```c
void xa_rcu_lock(void);
void xa_rcu_unlock(void);
```

进入/退出 RCU 读端临界区。在 `XA_CONFIG_RCU` 下使用游标 API 进行读操作时必须调用。

未定义 `XA_CONFIG_RCU` 时编译为空操作。

**配合使用：** `xa_rcu_lock` / `xa_rcu_unlock` 包围所有游标读操作：`xas_load`、
`xas_find`、`xas_find_marked`、`xas_get_mark`。在临界区内使用 `xas_retry`
处理并发写入。

---

## 值条目

值条目将无符号小整数直接存储在槽中，无需堆分配。整数通过左移 2 位并设置值标志来编码。

### `xa_mk_value`

```c
void *xa_mk_value(unsigned long v);
```

将整数 `v` 编码为值条目。可存储的最大值为 `UINTPTR_MAX >> 2`
（64 位系统上为 2^62 - 1）。

---

### `xa_to_value`

```c
unsigned long xa_to_value(void *entry);
```

将值条目解码回整数。仅在 `xa_is_value(entry)` 为 true 时有效。

---

### `xa_is_value`

```c
bool xa_is_value(void *entry);
```

测试 `entry` 是否为值条目（而非普通指针）。

**配合使用：** 这三个值条目函数形成一个始终配合使用的三件套：

1. `xa_mk_value(v)` — 将整数编码为可存储的条目。
2. `xa_store(&xa, idx, xa_mk_value(v), 0)` — 存储它。
3. 检索时：`xa_is_value(e)` 检查，`xa_to_value(e)` 解码。

这允许你存储小整数（64 位上最大 2^62 - 1）而无需分配内存。
值条目和指针条目可以混合存在于同一个 xarray 中——用 `xa_is_value` 区分它们：

```c
xa_store(&xa, 0, xa_mk_value(42), 0);   // 值条目
xa_store(&xa, 1, malloc(64), 0);         // 指针条目

uint64_t idx;
void *e;
xa_for_each(&xa, idx, e) {
    if (xa_is_value(e))
        printf("[%lu] 值 = %lu\n", idx, xa_to_value(e));
    else
        printf("[%lu] 指针 = %p\n", idx, e);
}
```

---

## 构建配置

所有配置均为编译时通过预处理器定义。在包含任何 xarray 头文件之前设置它们。

| 定义                  | 效果                                                    |
|-----------------------|---------------------------------------------------------|
| `XA_CONFIG_LOCK`      | 在 `struct xarray` 中嵌入锁；简单 API 自动获取。           |
| `XA_CONFIG_RCU`       | 启用 RCU 读端区域和延迟节点释放。                          |
| `XA_CUSTOM_ALLOC`     | 用户提供 `xa_alloc_fn` / `xa_free_fn` 替代 malloc/free。  |
| `XA_CUSTOM_LOCK`      | 用户提供锁类型和操作（需要 `XA_CONFIG_LOCK`）。             |
| `XA_CUSTOM_RCU`       | 用户提供 RCU 读锁和 call_rcu（需要 `XA_CONFIG_RCU`）。     |
| `XA_CUSTOM_BARRIERS`  | 用户提供 `xa_smp_rmb` / `xa_smp_wmb` / `xa_smp_mb` 宏。  |

### 默认实现

| 功能     | 默认                                                        |
|----------|-------------------------------------------------------------|
| 内存分配 | `malloc` + `memset(0)` / `free`                              |
| 锁       | `pthread_mutex_t`                                            |
| RCU      | 桩实现（立即回调，不实际延迟）                                  |
| 内存屏障 | 架构自动检测：x86 编译器屏障，ARM64 `dmb`，RISC-V `fence`，回退到 `__atomic_thread_fence` |

### 内存分配契约

`xa_alloc_fn(size)` 必须返回指向至少 `size` 字节**已清零内存**的指针，
失败时返回 `NULL`。`xa_free_fn(ptr)` 释放先前由 `xa_alloc_fn` 分配的内存。
`xa_free_fn(NULL)` 必须是安全的空操作。
