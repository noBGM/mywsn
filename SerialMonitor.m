function SerialMonitor()
    % 全局变量
    serialObj = [];
    %parser = HDLCParser();
    isRunning = false;
    byteCount = 0;
    
    try
        % 获取可用串口列表
        ports = serialportlist("available");
        if isempty(ports)
            disp('未找到可用串口');
            return;
        end
        
        % 显示所有可用端口
        disp(['可用串口: ' strjoin(ports, ', ')]);
        
        % 默认使用COM4，如果不存在则使用第一个可用端口
        if any(strcmp(ports, 'COM4'))
            portName = 'COM4';
        else
            portName = ports(1);
        end
        
        % 打开串口
        disp(['正在打开串口: ' portName]);
        serialObj = serialport(portName, 115200, 'Timeout', 5);
        
        % 清空缓冲区
        flush(serialObj);
        
        % 设置标志
        isRunning = true;
        
        % 显示串口配置
        disp('串口配置:');
        disp(serialObj);
        
        % 开始读取数据
        disp('开始读取数据 (按Ctrl+C停止)...');
        
        while isRunning
            
            % 读取单个字节
            byte = read(serialObj, 1, "uint8");
            byteCount = byteCount + 1;
            fprintf('字节 #%d: 0x%02X [%d]\n', byteCount, byte, byte);
            %frame = parser.parse_byte(byte);
            % 显示原始数据（十六进制和十进制）
            %fprintf('收到HDLC帧: %d\n', frame);
            % 每10个字节显示一个分隔符
            if mod(byteCount, 10) == 0
                disp('------------------------');
            end
            pause(0.01); % 短暂暂停以减少CPU使用
        end
        
    catch e
        disp(['错误: ' e.message]);
        disp(getReport(e));
    end
    
    % 清理
    if ~isempty(serialObj) && isvalid(serialObj)
        delete(serialObj);
        disp('串口已关闭');
    end
end