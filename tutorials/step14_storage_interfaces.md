# Step 14：定义 IStorage / ICache 抽象

本步骤目标：先定义业务层依赖的存储接口和缓存接口，让后续注册、登录、私聊、群聊这些业务代码不直接依赖 SQLite。

Step 14 只做接口和 no-op cache：

- 定义存储领域类型。
- 定义 `IStorage`。
- 定义 `ICache`。
- 实现 `NullCache`。

本 Step 不实现 `SQLiteStorage`，不执行 SQL，不写注册登录逻辑，也不保存真实消息。SQLite 落地放在 Step 15。

## 1. 为什么先定义存储接口

后续业务层会需要这些能力：

- 注册时创建用户。
- 登录时按用户名查用户。
- 私聊时保存消息。
- 群聊时查询群成员并保存群消息。
- 用户上线时读取离线消息。

如果业务代码直接调用 SQLite：

```text
AuthService
    ↓ 直接拼 SQL
SQLite
```

业务规则和数据库细节就会混在一起，测试也必须准备真实数据库文件。

Step 14 先定义接口：

```text
AuthService / ChatService / GroupService
    ↓ depends on
IStorage
    ↓ implemented later
SQLiteStorage
```

这样后续业务层只关心“我要创建用户、保存消息、查询历史”，不关心底层是 SQLite、内存替身还是别的存储。

## 2. 本 Step 新增和修改的文件

新增：

```text
include/liteim/storage/StorageTypes.hpp
include/liteim/storage/IStorage.hpp
include/liteim/storage/ICache.hpp
include/liteim/storage/NullCache.hpp
src/storage/NullCache.cpp
tests/test_storage_interfaces.cpp
tutorials/step14_storage_interfaces.md
```

修改：

```text
src/CMakeLists.txt
tests/CMakeLists.txt
tests/test_main.cpp
README.md
docs/architecture.md
docs/database.md
docs/project_layout.md
docs/interview_notes.md
tutorials/README.md
```

## 3. StorageTypes.hpp 讲解

`StorageTypes.hpp` 放存储层和业务层共享的数据类型。

### UserId / GroupId / MessageId / UnixTimestamp

```cpp
using UserId = std::uint64_t;
using GroupId = std::uint64_t;
using MessageId = std::uint64_t;
using UnixTimestamp = std::int64_t;
```

作用：

- 给用户、群组、消息和时间戳一个明确类型名。
- 让接口读起来像业务语义，而不是到处写 `std::uint64_t`。

边界：

- 这些只是 C++ 类型别名，不负责生成 ID。
- 真实 ID 来源以后由 `SQLiteStorage` 根据数据库自增主键或插入结果决定。

### UserType

```cpp
enum class UserType : std::uint8_t {
    Human = 0,
    Bot = 1,
};
```

作用：

- 区分普通用户和未来 PersonaAgent / Bot 用户。

边界：

- Step 14 只定义类型。
- Bot 接入和 AI 逻辑不在本 Step。

### User / CreateUserRequest

`User` 表示已经存在的用户记录，包含 `id`、`username`、`nickname`、密码 salt/hash、用户类型和创建时间。

`CreateUserRequest` 表示创建用户时需要传入的数据，不包含 `id` 和 `created_at`，因为这些通常由存储层生成。

### Group / CreateGroupRequest

`Group` 表示群组记录，包含群 ID、群名、群主 ID 和创建时间。

`CreateGroupRequest` 表示创建群时的输入，只需要群名和群主 ID。

### PrivateMessage / SavePrivateMessageRequest

`PrivateMessage` 表示已经保存的私聊消息，包含消息 ID、发送者、接收者、消息体、创建时间和是否已投递。

`SavePrivateMessageRequest` 表示保存私聊消息时的输入，不包含消息 ID，因为 ID 由存储层生成。

`delivered` 用于区分消息是否已经投递给接收方。后续离线消息查询会用到这个字段。

### GroupMessage / SaveGroupMessageRequest

`GroupMessage` 表示已经保存的群消息。

`SaveGroupMessageRequest` 表示保存群消息时的输入。

群消息没有单个 `receiver_id`，因为接收者是一组群成员。

## 4. IStorage.hpp 讲解

`IStorage` 是一个纯抽象接口：

```cpp
class IStorage {
public:
    virtual ~IStorage() = default;
    ...
};
```

析构函数是 `virtual`，这样以后可以通过 `std::unique_ptr<IStorage>` 持有 `SQLiteStorage` 或测试替身，并正确析构派生类。

### createUser()

```cpp
std::optional<User> createUser(const CreateUserRequest& request);
```

作用：

- 创建用户。

输入：

- 用户名、昵称、密码 salt、密码 hash、用户类型。

输出：

- 成功时返回完整 `User`。
- 失败时返回 `std::nullopt`，例如用户名重复或输入不合法。

副作用：

- 真实实现会写入数据库。

### findUserByUsername()

```cpp
std::optional<User> findUserByUsername(const std::string& username) const;
```

作用：

- 登录时按用户名查用户。

输出：

- 找到返回 `User`。
- 找不到返回 `std::nullopt`。

### findUserById()

```cpp
std::optional<User> findUserById(UserId user_id) const;
```

作用：

- 按用户 ID 查询用户。

使用场景：

- 查询好友列表时补用户信息。
- 群成员 ID 转用户信息。

### addFriendship()

```cpp
bool addFriendship(UserId user_id, UserId friend_id);
```

作用：

- 建立两个用户之间的好友关系。

输出：

- 成功返回 `true`。
- 用户不存在、关系非法或存储失败返回 `false`。

边界：

- 是否允许自己加自己、是否双向插入，由后续真实实现和业务规则约定。

### getFriends()

```cpp
std::vector<User> getFriends(UserId user_id) const;
```

作用：

- 查询某个用户的好友列表。

输出：

- 返回好友用户信息列表。
- 没有好友时返回空 vector。

### createGroup()

```cpp
std::optional<Group> createGroup(const CreateGroupRequest& request);
```

作用：

- 创建群组。

输入：

- 群名。
- 群主用户 ID。

输出：

- 成功返回 `Group`。
- 群主不存在或创建失败返回 `std::nullopt`。

### addGroupMember()

```cpp
bool addGroupMember(GroupId group_id, UserId user_id);
```

作用：

- 把用户加入群组。

输出：

- 成功返回 `true`。
- 群或用户不存在、重复加入或存储失败返回 `false`。

### removeGroupMember()

```cpp
bool removeGroupMember(GroupId group_id, UserId user_id);
```

作用：

- 从群组中移除用户。

输出：

- 实际移除成功返回 `true`。
- 群不存在、用户不在群中或存储失败返回 `false`。

### getGroupMembers()

```cpp
std::vector<UserId> getGroupMembers(GroupId group_id) const;
```

作用：

- 查询一个群有哪些成员。

输出：

- 返回成员用户 ID。
- 群不存在或没有成员时返回空 vector。

### findGroupById()

```cpp
std::optional<Group> findGroupById(GroupId group_id) const;
```

作用：

- 按 ID 查询群组。

输出：

- 找到返回 `Group`。
- 找不到返回 `std::nullopt`。

### savePrivateMessage()

```cpp
std::optional<PrivateMessage> savePrivateMessage(
    const SavePrivateMessageRequest& request);
```

作用：

- 保存一条私聊消息。

输出：

- 成功返回带消息 ID 的 `PrivateMessage`。
- 用户不存在或存储失败返回 `std::nullopt`。

### saveGroupMessage()

```cpp
std::optional<GroupMessage> saveGroupMessage(
    const SaveGroupMessageRequest& request);
```

作用：

- 保存一条群聊消息。

输出：

- 成功返回带消息 ID 的 `GroupMessage`。
- 群不存在、发送者不存在或存储失败返回 `std::nullopt`。

### getPrivateHistory()

```cpp
std::vector<PrivateMessage> getPrivateHistory(
    UserId first_user_id,
    UserId second_user_id,
    std::size_t limit,
    std::size_t offset) const;
```

作用：

- 查询两个用户之间的私聊历史。

输入：

- 两个用户 ID。
- `limit`：最多返回多少条。
- `offset`：从第几条开始返回。

输出：

- 返回历史消息列表。
- 没有历史时返回空 vector。

### getGroupHistory()

```cpp
std::vector<GroupMessage> getGroupHistory(
    GroupId group_id,
    std::size_t limit,
    std::size_t offset) const;
```

作用：

- 查询群聊历史。

输出：

- 返回群消息列表。
- 群不存在或没有历史时返回空 vector。

### getOfflineMessages()

```cpp
std::vector<PrivateMessage> getOfflineMessages(
    UserId user_id,
    std::size_t limit) const;
```

作用：

- 查询用户未投递的离线私聊消息。

输出：

- 返回未投递消息列表。
- 没有离线消息时返回空 vector。

边界：

- Step 14 只定义查询接口。
- 标记消息已投递、离线群消息策略等后续按业务需要补充。

## 5. ICache.hpp 和 NullCache 讲解

### setOnline()

```cpp
void setOnline(UserId user_id, int session_fd);
```

作用：

- 记录某个用户当前在线，并关联到一个 session fd。

在 `NullCache` 中：

- 什么都不做。

### setOffline()

```cpp
void setOffline(UserId user_id);
```

作用：

- 记录某个用户离线。

在 `NullCache` 中：

- 什么都不做。

### findOnlineSession()

```cpp
std::optional<int> findOnlineSession(UserId user_id) const;
```

作用：

- 查询用户当前是否在线，以及对应 session fd。

在 `NullCache` 中：

- 永远返回 `std::nullopt`。

### clear()

```cpp
void clear();
```

作用：

- 清空缓存。

在 `NullCache` 中：

- 什么都不做。

`NullCache` 的意义不是提供真实缓存，而是给单机版一个默认实现，让业务层可以先写成依赖 `ICache&`，后续再替换成真实缓存。

## 6. CMake 变化

`src/CMakeLists.txt` 新增：

```cmake
add_library(liteim_storage
    storage/NullCache.cpp
)
```

`liteim_storage` 暴露统一 include root：

```cmake
target_include_directories(liteim_storage PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)
```

当前 `liteim_storage` 不链接 SQLite，因为 Step 14 没有真实数据库实现。

测试目标 `liteim_tests` 链接 `liteim_storage`，用于编译并验证接口和 `NullCache`。

## 7. 测试说明

本 Step 新增：

```text
tests/test_storage_interfaces.cpp
```

测试分成三类。

### 测试 1：接口是抽象契约

用例名：

```text
storage interfaces are abstract contracts
```

验证内容：

- `IStorage` 是抽象类。
- `ICache` 是抽象类。
- 二者都有 virtual destructor。
- `NullCache` 实现了 `ICache`。

为什么要测：

这能防止接口误写成普通类，也能保证后续通过基类指针析构派生实现时不会出错。

### 测试 2：测试替身能实现完整 IStorage

用例名：

```text
storage test double implements full contract
```

验证内容：

- 测试文件内部的 `FakeStorage` 能继承并实现完整 `IStorage`。
- 用户创建和查询能通过接口表达。
- 好友、群组、群成员、私聊消息、群聊消息、历史查询、离线消息都能通过接口表达。

为什么要测：

Step 14 的核心不是数据真的存进 SQLite，而是接口足够完整，后续业务层可以用测试替身验证业务逻辑。

### 测试 3：NullCache 是 no-op

用例名：

```text
NullCache is a no-op cache
```

验证内容：

- 调用 `setOnline()` 不会保存在线状态。
- 调用 `setOffline()` 不会报错。
- 调用 `clear()` 不会报错。
- `findOnlineSession()` 始终返回空。

为什么要测：

`NullCache` 是单机默认缓存实现，它应该安全、可重复调用，并且不会悄悄保存状态。

### 如何运行测试

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/liteim_tests
```

测试通过说明：

- storage 模块能独立编译。
- `IStorage` / `ICache` 的接口签名完整。
- `NullCache` 符合 no-op 语义。
- 后续业务层可以依赖这些接口继续实现。

## 8. 面试时怎么讲

可以这样说：

> 我在业务层真正实现注册登录之前，先定义了 `IStorage` 和 `ICache`。`IStorage` 覆盖用户、好友、群组、私聊消息、群聊消息、历史和离线消息等能力；`ICache` 表达在线 session 查询的缓存边界。业务层以后只依赖接口，真实环境用 `SQLiteStorage`，测试环境可以用 fake storage。这样业务逻辑不会和 SQLite 细节耦合。

重点讲清楚：

- `IStorage` 是持久化能力，不是 SQLite 本身。
- `SQLiteStorage` 是后续对 `IStorage` 的一种实现。
- `ICache` 是在线状态缓存边界。
- `NullCache` 是 no-op 默认实现。
- 测试替身能让业务层测试不依赖真实数据库。

## 9. 面试常见追问

### 为什么业务层不直接依赖 SQLite？

因为注册、登录、聊天这些业务规则和 SQL 细节是两个变化方向。直接依赖 SQLite 会让业务测试变慢、环境复杂，也让未来替换存储更困难。

### `IStorage` 和 `SQLiteStorage` 的关系是什么？

`IStorage` 是接口，定义业务层需要什么能力；`SQLiteStorage` 是后续 Step 15 的具体实现，负责把这些能力翻译成 SQLite 操作。

### 为什么 Step 14 不直接写 SQLiteStorage？

这是分步教学项目。Step 14 先稳定接口，Step 15 再实现数据库。这样可以先讲清楚抽象边界，再讲 SQL 和持久化细节。

### `NullCache` 有什么用？

它让业务层可以从一开始依赖 `ICache`，但单机版不用引入真实缓存。以后如果要接 Redis 或进程内在线表，可以替换实现而不是改业务接口。

### 为什么测试里要写 FakeStorage？

它证明 `IStorage` 不是只停留在声明上，而是真的可以被业务测试替身实现。后续写 `AuthService`、`ChatService` 测试时，可以复用这个思路，不必启动 SQLite。
