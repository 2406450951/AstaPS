# IL2CPP Dump 笔记（6.6 dump，2026-10-30）

## 一、dump 从哪来

公开仓库：`zhongyeernai402-cloud/genshin-il2cpp-dump`

```
6.6dump.zip    26 MB（压缩）→ 解压出 6.6dump.cs  199 MB
```

**这是 6.6 版，不是 7.1。** 类名/字段名/结构布局 90% 通用，**RVA 地址必须重新验证**。

## 二、本机怎么拿到的（重要技巧）

26 MB 的 zip 从这台机器下不动（7 个镜像全部截断）。最终做法：

1. 用 `curl -C -` 循环续传，三个镜像轮流，20 轮爬到 **13.5 MB**
2. 这 13.5 MB 是**文件开头**，zip 的第一个条目就是 `6.6dump.cs`
3. 手动解析 local file header，用 `zlib.decompressobj(-15)` 流式解压
4. 即使数据截断，也解出了 **128.5 MB** 的 `6.6dump.cs`

脚本思路（可复用）：

```python
idx = data.find(b'PK\x03\x04')
sig, ver, flags, method, mt, md, crc, csize, usize, nlen, elen = struct.unpack('<IHHHHHIIIHH', data[idx:idx+30])
name  = data[idx+30: idx+30+nlen]
blob  = data[idx+30+nlen+elen : idx+30+nlen+elen+csize]   # 可能被截断
d = zlib.decompressobj(-15)                                # raw deflate
plain = d.decompress(blob)  # 截断会抛异常，要按 64KB 分块 try 累积
```

**结论：大文件下载被限速时，能抓到前半段就有救，别整份重下。**

## 三、dump 里已确认的关键类

### 相机（解释了为什么我们的探测全失败）

```
miHoYoCamera.CameraStateMgr              相机状态机（0x58 字节）
miHoYoCamera.BaseCameraState             相机状态基类（0x110）
miHoYoCamera.CameraBlendState            平滑过渡（0x110）
miHoYoCamera.CameraStack                 状态栈（0x18）
MoleMole.MonoCameraWorldShiftHelper      ★ 浮点原点助手
```

`MonoCameraWorldShiftHelper` 的字段：

```csharp
public Vector3 initPosition;                                  // Offset: 0x18
public bool ShiftWorld(Vector3 from, Vector3 to);             // RVA: 0xAF7BC90（6.6）
```

**这就是"浮点原点"的铁证**：相机和渲染都工作在 `world - initPosition` 的相对空间里。
所以我们之前"按世界坐标找相机矩阵"的判据必然失效——不是方法错，是坐标系不同。

### 碰撞

```
MoleMole.MonoWallCollisionHelper          墙面碰撞回调（0x48）
MoleMole.MonoEntityLayerCollisionHelper   ★ 碰撞层控制（0x20）
    public JNFEKCIJENF type;             // Offset: 0x18
    public int GetCollisionLayer();       // RVA: 0x6C60F40（6.6）
    static int?[] collisionType2Layer;
SpringMagicBone                           布料碰撞（0xC0）
    public bool EnablePlaneCollision;     // 0x34
    public bool EnableCapsuleCollision;   // 0x68
```

`MonoEntityLayerCollisionHelper.GetCollisionLayer()` 是**最可能的穿墙切入点**：
把实体的碰撞层改成一个"不与任何东西碰撞"的层，角色就能穿过墙。

## 四、7.1 上怎么用这份 6.6 dump

**核心限制**：RVA 不能直接用。但有三条路可以用上：

### 路线 A：按名字找结构（最稳）

```
1. 在 7.1 客户端里搜"类型名字符串"（IL2CPP 元数据里有类名字符串，
   即使加密，运行时也会解密成 C 字符串放在内存里）
2. 从类名字符串反查 Il2CppClass 结构
3. 该类有 vtable / static_fields 指针 → 顺着拿到实例
```

这条路只需要"类名字符串"，不需要完整元数据。

### 路线 B：按机器码特征扫（pattern scan）

`KitsukiDev/hk4e-patch-universal` 就是这种思路（版本无关的客户端补丁）。
做法：从 6.6 的 dump 拿到函数语义，在 7.1 的 exe 里按指令特征搜索相似函数。

### 路线 C：硬件断点反查（我们已验证过的工具）

```
1. 已知玩家对象（我们之前扫到过：坐标 + 血量）
2. 在该对象的内存周围找"指向 MonoEntityLayerCollisionHelper 的指针"
3. 断点读写它 → 抓指令 → 反汇编确认偏移
```

## 五、下一步的具体第一步

**目标：把角色碰撞层改成"不碰撞"**

```
1. 用 6.6 dump 确认 MonoEntityLayerCollisionHelper 的完整字段（type 枚举值）
2. 在 7.1 客户端里搜 "MonoEntityLayerCollisionHelper" 字符串
3. 字符串 → Il2CppClass → 实例（角色身上的那个组件）
4. 读 type 字段 → 改成"不参与碰撞"的值
5. 验证：能否穿墙、能否落地
```

预计需要的工具：都齐了（内存扫描 + 硬件断点 + 反汇编 + 这份 dump）。

## 六、这份 dump 还能查到什么

`6.6dump.cs` 是**全量类型定义**，任何"某个功能对应哪个类/字段"的问题都能查：

```powershell
# 查类
Select-String -Path 6.6dump.cs.part -Pattern '^public class .*关键字'

# 查字段（带偏移）
Select-String -Path 6.6dump.cs.part -Pattern '字段名; // Offset:'

# 查方法（带 RVA）
Select-String -Path 6.6dump.cs.part -Pattern '方法名\('
```

已用它确认过：`SpringMagicBone` 的碰撞开关、`MonoWallCollisionHelper` 的回调表、
`MonoEntityLayerCollisionHelper` 的层控制、`MonoCameraWorldShiftHelper` 的浮点原点。
