# Git 协作指南：两种工作模式

---

## 方法一：拉取他人仓库，修改后推到自己仓库

> **场景**：你看到别人的好项目，想拉下来改一改，推到自己仓库保存。

### 1. 克隆他人仓库

```bash
git clone https://github.com/别人/项目.git
cd 项目
```

### 2. 修改 remote —— fetch 从原仓库拉，push 推到自己仓库

```bash
# 查看当前 remote
git remote -v

# 保持 origin 的 fetch 不变（从原仓库拉取更新）
# 修改 origin 的 push 地址为自己的仓库
git remote set-url --push origin https://github.com/你的用户名/你的仓库.git

# 验证配置（fetch 和 push 各指向不同地址）
git remote -v
```

输出应该类似：

```
origin  https://github.com/别人/项目.git (fetch)
origin  https://github.com/你的用户名/你的仓库.git (push)
```

### 3. 日常使用

```bash
# 拉取原仓库最新代码
git pull origin main

# 推送修改到自己的仓库
git push origin main
```

### 4. （可选）添加第二个 remote，更清晰

```bash
# 把原仓库加为 upstream
git remote add upstream https://github.com/别人/项目.git

# 把自己仓库设为 origin
git remote set-url origin https://github.com/你的用户名/你的仓库.git
```

这样更清晰：

- `git pull upstream main` → 拉取原仓库
- `git push origin main` → 推送到自己仓库

---

## 方法二：创建自己仓库，协作者 Fork + PR，你审核合并

> **场景**：你是项目维护者，想让别人贡献代码，你来审核把关。

### 第一步：创建主仓库

1. 打开 https://github.com/new
2. 填入仓库名、描述，选择 Public（别人才能拉取）
3. **不要勾选** README / .gitignore / License（如果本地已有代码）
4. 点击 Create repository

### 第二步：推送本地代码到仓库

```bash
git remote add origin https://github.com/你的用户名/仓库名.git
git branch -M main
git push -u origin main
```

### 第三步：协作者 Fork 你的仓库

协作者打开你的仓库页面 → 点击右上角 **Fork** → Fork 到自己的账户下。

### 第四步：协作者克隆自己的 Fork，修改后提交

```bash
git clone https://github.com/协作者/仓库名.git
cd 仓库名

# 修改代码...

git add .
git commit -m "描述修改内容"
git push origin main
```

### 第五步：协作者提交 Pull Request

协作者回到自己的 Fork 页面 → 点击 **Contribute** → **Open Pull Request** → 填写说明 → **Create Pull Request**。

### 第六步：你（维护者）审核合并

1. 打开仓库的 **Pull Requests** 标签
2. 点击对应的 PR
3. 查看 **Files changed** 检查代码差异
4. 可选：在代码行上添加 Review 意见
5. 审核通过后点击 **Merge pull request**

### 保护分支（推荐）

在仓库 **Settings → Branches → Add rule** 中设置：

- Branch name: `main`
- ☑ Require a pull request before merging
- ☑ Require approvals（至少 1 人审批）

这样没人能直接推送到 main，所有改动都必须通过 PR + 审核。

---

## 两种方法对比

| | 方法一 | 方法二 |
|---|---|---|
| **适用场景** | 单向拉取，自己改 | 多人协作，你审核 |
| **权限控制** | 无 | PR + Review |
| **分支保护** | 不需要 | 推荐开启 |
| **代码流向** | 别人 → 你 | 你 → 别人 → PR → 你审核 |

---

## 常见问题

### Q: `git push` 报 403 Permission denied
**原因**：remote 的 push 地址指向了别人的仓库，你没有写入权限。  
**解决**：用方法一的 `git remote set-url --push` 把 push 地址改为自己的仓库。

### Q: TLS certificate warning
**解决**：`git config --global http.sslVerify true` 重新启用证书验证。

### Q: 如何同时贡献给别人和自己？
给别人的仓库提 PR（方法二的协作者视角），同时自己仓库保存一份（方法一）。
