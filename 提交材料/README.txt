提交材料分类说明

01_论文
  - 论文_矩阵连乘元素的逼近.docx：按竞赛模板重新生成的论文源文件。
  - 论文_矩阵连乘元素的逼近.pdf：由docx导出的PDF版本。

02_算法源码
  - approx_cor.cpp：方式2主导路线枚举/稀疏矩阵迭代算法实现。
  - Makefile：C++编译脚本。
  - verify_selected_results.py：对高分样例进行完整LAT转移枚举验证。
  - final_scores.py：统一计算并导出有效估计值得分。

03_实验结果
  - scores.txt：最终得分统计结果。
  - results.txt：原始实验结果记录。

04_题目与模板
  - 2026密码数学挑战赛-赛题三.pdf：赛题原文。
  - 2026muban.doc：论文模板。
  - 附件1参考文献著录规则.pdf：参考文献格式规则。

复现实验建议
  1. 编译算法：进入02_算法源码目录后执行make。
  2. 验证高分条目：执行python verify_selected_results.py。
  3. 重新计算得分：执行python final_scores.py。
