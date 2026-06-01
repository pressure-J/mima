# CLAUDE.md - 密码数学挑战赛论文写作指南

## 项目概述

本项目是**第十一届（2026年）全国高校密码数学挑战赛赛题三**的参赛论文。

### 赛题信息
- **赛题名称**: 矩阵连乘元素的逼近
- **核心问题**: 针对SPN结构密码算法的线性分析，设计高效算法逼近相关矩阵元素
- **密码算法**: HS(r) - 一个基于4-bit S盒的轻量级SPN置换，定义在F_2^48上
- **矩阵规模**: M(r) ∈ R^{2^32 × 2^32}

---

## 赛题核心要求

### 1. 任务定义
给定r轮密码置换HS(r)的相关矩阵M(r)，对于指定的输入掩码u和输出掩码v，设计**方式2**算法逼近M(r)[v,u]的真实值。

### 2. 算法要求
- 复杂度**严格低于**方式1（方式1需要2^32次计算）
- 算法必须**通用**，对任意轮数r都适用
- 不能硬编码方式1的结果

### 3. 有效估计值条件
同时满足以下4个条件：
```
|VE - VT| <= |VT| × 2^(-2r)
VE ≠ 0
u ≠ 0
v ≠ 0
```

### 4. 评分公式
单条分数 = log2(2^(2r) × |VE|)

---

## 密码算法结构

### HS(r)置换定义
```
输入: x = (x0, x1, ..., x7), xi ∈ F_2^4
轮函数: F = MC ∘ SR ∘ SC

组件:
1. SC (S盒层): 8个并行的4-bit S盒
   S = [C,6,9,0,1,A,2,B,3,8,5,D,4,E,7,F]

2. SR (行移位):
   (y0,y1,y2,y3,y4,y5,y6,y7) = (x0,x5,x2,x7,x4,x1,x6,x3)

3. MC (列混合):
   y0 = x0⊕x2⊕x3,  y1 = x0
   y2 = x1⊕x2,      y3 = x0⊕x2
   y4 = x4⊕x6⊕x7,  y5 = x4
   y6 = x5⊕x6,      y7 = x4⊕x6
```

### 相关矩阵定义
```
M(r)[v,u] = 2^{-32} × Σ_{x∈F_2^32} (-1)^{u^T·x ⊕ v^T·HS(x)}
```

---

## 论文写作要求

### 一、文件格式
- **源文件**: Word (.docx) 或 LaTeX
- **PDF文件**: 必须同时提供
- **行距**: 1.5倍行距
- **标点**: 半角标点
- **目录**: 无需目录

### 二、页面设置
- 页码：页面下方居中，从标题页起至参考文献最后一页

### 三、字体规范

| 项目 | 字体 | 字号 |
|------|------|------|
| 论文标题 | 黑体（不加黑） | 小二 |
| 作者 | 仿宋 | 四号 |
| 学校/邮箱 | 宋体 | 小四 |
| "摘要：" | 宋体加黑 | 四号 |
| 摘要内容 | 宋体 | 四号 |
| 一级标题 | 黑体 | 三号 |
| 二级标题 | 黑体 | 小三 |
| 三级标题 | 黑体 | 小三 |
| 正文 | 宋体 | 四号 |
| 参考文献标题 | 黑体 | 四号 |
| 参考文献内容 | 宋体 | 四号 |

### 四、标题层级
```
一、(一级标题，中文序号，顶格三号黑体)
  1.(二级标题，数字序号，缩进2格小三黑体)
    1.1(三级标记，数字序号，缩进2格小三黑体)
正文(四号宋体)
```

### 五、公式编号
- 格式：(x.y)，右对齐
- x：一级标题序号
- y：该一级标题下的流水编号
- 例：一级标题"二、"下的第5个公式编号为(2.5)

### 六、图表规范
- 图序号：阿拉伯数字顺序编号（图1，图2...）
- 图题：图的正下方
- 表序号：与图编号方式一致
- 表题：简练明确
- 表头：不使用斜线

### 七、物理量排版
- 物理量符号：斜体（如重力加速度g）
- 单位、函数符号：正体（如π，ln，e）
- 向量、矩阵：黑斜体（如向量b，矩阵B，用新罗马体）

---

## 参考文献著录规则（GB/T 7714-2015）

### 正文引用格式
使用上角标：`……已有综述[25，26～30]。`

### 参考文献格式

#### 专著 [M]
```
[序号] 主要责任者.题名:其他提名信息[M].其他责任者.版本项.出版社所在地:出版社名,出版年.
```

**示例：**
```
[1] 同济大学数学科学学院.高等数学:上册[M].8版.北京:高等教育出版社,2023.
[2] 哈里森,沃尔德伦.经济数学与金融数学[M].谢远涛,译.北京：中国人民大学出版社,2012.
[3] BAKER S K,JACKSON M E. The future of resource sharing[M].3rd ed. New York:The Haworth Press,1995.
```

#### 期刊 [J]
```
[序号] 主要责任者.题名:其他提名信息[J].出版社名,出版年,卷（期）:引文页码.
```

**示例：**
```
[4] 袁训来,陈哲,肖书海,等.蓝田生物群:一个认识多细胞生物起源和早期演化的新窗口[J].科学通报,2012,55(34):3219.
[5] KANAMORI H.Shaking without quaking[J].Science,1998,279(5359):2063.
```

#### 会议论文 [C]
```
[序号] 主要责任者.题名[C]//会议录名.出版地:出版者,出版年:页码.
```

#### 学位论文 [D]
```
[序号] 主要责任者.题名[D].保存地:保存单位,年份.
```

#### 电子资源 [EB/OL]
```
[序号] 主要责任者.题名[EB/OL].(发布日期)[引用日期].获取路径.
```

---

## 论文结构建议

### 摘要（300-500字）
- 研究目的
- 研究方法
- 主要结果
- 创新点

### 关键词（3-5个）
建议：线性分析、相关矩阵、矩阵连乘逼近、SPN密码结构、MILP优化

### 一、引言
1. 密码分析背景
2. 线性分析的意义
3. 问题描述与研究目标
4. 论文组织结构

### 二、预备知识
1. 线性分析基础
   - 相关度定义
   - 相关矩阵性质
2. SPN密码结构
   - S盒层
   - 线性层
3. 矩阵连乘与路径表示

### 三、逼近算法设计
1. 算法思路
   - 主导路线方法
   - 路径搜索策略
2. 算法描述
   - 伪代码
   - 复杂度分析
3. 理论分析
   - 精度保证
   - 与方式1的复杂度对比

### 四、实验结果与分析
1. 实验设置
   - 参数选择（r, u, v）
   - 实验环境
2. 实验结果
   - 有效估计值列表
   - 与精确值的对比
3. 结果分析
   - 精度统计
   - 得分计算

### 五、结论
1. 主要贡献
2. 算法优势
3. 未来工作

### 参考文献

### 附录
- 完整代码
- 详细数据

---

## 算法设计思路（核心）

### 方法1：主导路线逼近
```
思路：找到相关度绝对值最大的几条路线，用它们的和逼近真实值

步骤：
1. 使用MILP/SAT求解器搜索高相关度路线
2. 按相关度绝对值排序
3. 取Top-K路线求和作为估计值

优点：直观，易于实现
缺点：可能遗漏重要路线
```

### 方法2：稀疏矩阵方法
```
思路：利用稀疏矩阵存储高相关度元素，减少计算量

步骤：
1. 计算单轮相关矩阵的稀疏表示
2. 使用稀疏矩阵乘法计算连乘积
3. 提取目标元素

优点：精确度高
缺点：当r较大时稀疏矩阵可能变稠密
```

### 方法3：基于MILP的路线搜索
```
思路：将路线搜索建模为混合整数线性规划问题

步骤：
1. 定义决策变量表示路线选择
2. 构建目标函数（最大化相关度）
3. 添加约束（路径连续性）
4. 求解MILP问题

优点：系统化搜索，可找到全局最优
缺点：求解时间可能较长
```

### 方法4：蒙特卡洛采样
```
思路：随机采样输入x，统计相关度

步骤：
1. 随机生成N个输入样本
2. 计算每个样本的 (-1)^{u^T·x ⊕ v^T·HS(x)}
3. 求平均值作为估计

优点：实现简单，复杂度可控
缺点：精度受样本量限制
```

### 推荐策略
**组合方法**：先用MILP搜索主导路线得到粗估计，再用蒙特卡洛采样细化。

---

## 参考文献列表（真实文献）

### 密码分析基础
[1] MATSUI M. Linear cryptanalysis method for DES cipher[C]//Advances in Cryptology — EUROCRYPT’93. Berlin: Springer, 1994: 386-397.

[2] NYBERG K. Linear approximation of block ciphers[C]//Advances in Cryptology — EUROCRYPT’94. Berlin: Springer, 1995: 439-444.

[3] CHABAUD F, VAUDENAY S. Linear cryptanalysis and differential cryptanalysis[C]//Advances in Cryptology — EUROCRYPT’94. Berlin: Springer, 1995: 368-378.

### 相关矩阵与线性壳
[4] NYBERG K. On the construction of highly nonlinear permutations[C]//Advances in Cryptology — EUROCRYPT’92. Berlin: Springer, 1993: 92-98.

[5] LEANDER G, POSCHMANN A. On the classification of 4-bit S-boxes[C]//Arithmetic of Finite Fields. Berlin: Springer, 2007: 159-176.

### MILP在密码分析中的应用
[6] MOUHA N, WANG Q, GU D, et al. Differential and linear cryptanalysis using mixed-integer linear programming[C]//Information Security and Cryptology. Berlin: Springer, 2012: 57-76.

[7] WU S, WANG M. Security evaluation against differential cryptanalysis using MILP[C]//信息安全与通信保密, 2013: 65-70.

[8] SUN S, HU L, WANG P, et al. Automatic security evaluation and (related-key) differential characteristic search: application to SIMON, PRESENT, LBlock, DES(L) and other bit-oriented block ciphers[C]//Advances in Cryptology — ASIACRYPT 2014. Berlin: Springer, 2014: 158-178.

### 轻量级密码设计
[9] BORGHOFF J, CANTEAUT A, GÜNEYSU T, et al. PRINCE – A low-latency block cipher for pervasive computing applications[C]//Advances in Cryptology — ASIACRYPT 2012. Berlin: Springer, 2012: 208-225.

[10] BEAULIEU R, SHORS D, SMITH J, et al. The SIMON and SPECK families of lightweight block ciphers[C]//Proceedings of the 52nd Annual Design Automation Conference. New York: ACM, 2015: 1-6.

[11] WU W, ZHANG L. LBlock: a lightweight block cipher[C]//Applied Cryptography and Network Security. Berlin: Springer, 2011: 327-344.

### 几何方法与高级分析
[12] BEYNE T. A geometric approach to linear cryptanalysis[C]//Advances in Cryptology — ASIACRYPT 2021. Berlin: Springer, 2021: 36-66.

[13] HU K, ZHANG C, CHANG C, et al. Unlocking mix-basis potential: geometric approach for combined attacks[C]//Advances in Cryptology — CRYPTO 2025. Berlin: Springer, 2025: 293-334.

[14] HU K, NIU Z, WANG M. Round-based approximation of (higher-order) differential-linear correlation[C]//Advances in Cryptology — EUROCRYPT 2026. Berlin: Springer, 2026.

### 矩阵计算与优化
[15] GOLUB G H, VAN LOAN C F. Matrix computations[M]. 4th ed. Baltimore: Johns Hopkins University Press, 2013.

[16] CORMEN T H, LEISERSON C E, RIVEST R L, et al. Introduction to algorithms[M]. 3rd ed. Cambridge: MIT Press, 2009.

### 密码分析工具
[17] SUN S, HU L, WANG M, et al. Towards finding the best characteristics of some bit-oriented block ciphers and automatic enumeration of (related-key) differential and linear characteristics with predetermined properties[J]. IACR Cryptology ePrint Archive, 2014: 747.

[18] ZOU J, WANG M, WU W, et al. MILP-based automatic differential analysis and its application to SIMON[J]. Chinese Journal of Electronics, 2015, 24(4): 816-822.

### 综合参考
[19] HEYS H M. A tutorial on linear and differential cryptanalysis[J]. Cryptologia, 2002, 26(3): 189-221.

[20] DAEMEN J, RIJMEN V. The design of Rijndael: AES — the advanced encryption standard[M]. Berlin: Springer, 2002.

---

## 工作流程

### Phase 1: 理解与准备
- [x] 阅读赛题PDF
- [x] 理解密码算法结构
- [x] 分析评分标准
- [ ] 编译运行C++参考代码

### Phase 2: 算法设计
- [ ] 实现方式1（精确计算）
- [ ] 设计方式2（逼近算法）
  - 主导路线搜索
  - MILP建模
  - 蒙特卡洛采样
- [ ] 验证算法正确性

### Phase 3: 实验与优化
- [ ] 选择参数组合(r,u,v)
- [ ] 运行实验收集数据
- [ ] 分析结果优化算法
- [ ] 计算得分

### Phase 4: 论文撰写
- [ ] 按模板格式撰写论文
- [ ] 插入公式、图表
- [ ] 整理参考文献
- [ ] 生成PDF文件

### Phase 5: 提交准备
- [ ] 整理代码
- [ ] 生成结果txt文件
- [ ] 检查格式规范
- [ ] 打包提交

---

## 注意事项

1. **引用必须真实**: 所有参考文献必须是真实存在的，可以通过Google Scholar、DBLP等验证

2. **格式严格匹配**: 论文格式必须与模板完全一致，包括字体、字号、行距、缩进等

3. **算法通用性**: 方式2算法必须对任意r都适用，不能只针对特定r设计

4. **代码完整性**: 提交的代码必须可编译运行，建议使用C++或Python

5. **得分计算**: 自己计算的得分要与考核组计算的一致

---

## Skills和MCP配置建议

### 推荐安装的Skills
- `web-search`: 用于搜索参考文献
- `pdf-reader`: 读取PDF文件（如pdftoppm）

### 推荐安装的MCP
- `scholar`: 学术文献搜索
- `latex`: LaTeX编译（如使用LaTeX排版）

### 环境配置
```bash
# 安装Python依赖
pip install numpy scipy matplotlib

# 安装C++编译器（如未安装）
# Windows: 安装MinGW或MSYS2
# 或使用Visual Studio

# 安装LaTeX（如使用LaTeX排版）
# Windows: 安装MiKTeX或TeX Live
```

---

## 快速开始

1. **理解赛题**: 仔细阅读 `赛题三/2026密码数学挑战赛-赛题三.pdf`
2. **运行参考代码**: 编译运行 `赛题三/computecor.cpp`
3. **设计算法**: 参考上述算法思路，实现方式2
4. **撰写论文**: 按照模板格式，参考论文结构
5. **整理提交**: 准备代码、数据、论文三份材料
