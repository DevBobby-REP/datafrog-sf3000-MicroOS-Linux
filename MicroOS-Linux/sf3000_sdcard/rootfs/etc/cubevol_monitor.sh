#!/bin/sh

# 进程名称或启动命令的一部分
PROCESS_NAME="/usr/bin/cubevol"
# 启动进程的完整命令
START_COMMAND="/usr/bin/cubevol"

# 无限循环，每5秒执行一次
while true; do
    # 使用ps和grep检测进程是否存在
    # 使用awk来计算匹配行的数量
    PROCESS_COUNT=$(ps aux | grep "$PROCESS_NAME" | grep -v grep | wc -l)

    if [ "$PROCESS_COUNT" -eq 0 ]; then
        echo "cubevol process does not exist, starting..."
        eval $START_COMMAND
    fi

    # 等待5秒再次检测
    sleep 5
done

