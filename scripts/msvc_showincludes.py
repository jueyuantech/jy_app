"""将 MSVC 的本地编码包含前缀转换为 Ninja 可稳定识别的 ASCII 前缀。"""

import subprocess
import sys

prefix = bytes.fromhex(sys.argv[1])
result = subprocess.run(sys.argv[2:], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
# 仅替换行首的依赖前缀，保留头文件路径及诊断信息的原始编码。
for line in result.stdout.splitlines(keepends=True):
    if line.startswith(prefix):
        line = b"Note: including file: " + line[len(prefix):]
    sys.stdout.buffer.write(line)
sys.stdout.buffer.flush()
sys.exit(result.returncode)
