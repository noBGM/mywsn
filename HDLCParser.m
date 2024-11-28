classdef HDLCParser < handle
    properties (Constant)
        HDLC_FLAG = hex2dec('7E')
        HDLC_ESCAPE = hex2dec('7D')
        HDLC_ESCAPE_MASK = hex2dec('20')
        HDLC_CRCINIT = hex2dec('FFFF')
        HDLC_CRCGOOD = hex2dec('F0B8')
    end
    
    properties
        buffer      % 数据缓冲区
        escaping    % 转义状态标志
        receiving   % 接收状态标志
        crc         % 当前CRC值
    end
    
    properties (Constant, Hidden)
        % CRC-CCITT (0xFFFF) 查找表
        fcstab = [
            hex2dec('0000'), hex2dec('1189'), hex2dec('2312'), hex2dec('329b'), hex2dec('4624'), hex2dec('57ad'), hex2dec('6536'), hex2dec('74bf'),
            hex2dec('8c48'), hex2dec('9dc1'), hex2dec('af5a'), hex2dec('bed3'), hex2dec('ca6c'), hex2dec('dbe5'), hex2dec('e97e'), hex2dec('f8f7'),
            hex2dec('1081'), hex2dec('0108'), hex2dec('3393'), hex2dec('221a'), hex2dec('56a5'), hex2dec('472c'), hex2dec('75b7'), hex2dec('643e'),
            hex2dec('9cc9'), hex2dec('8d40'), hex2dec('bfdb'), hex2dec('ae52'), hex2dec('daed'), hex2dec('cb64'), hex2dec('f9ff'), hex2dec('e876'),
            hex2dec('2102'), hex2dec('308b'), hex2dec('0210'), hex2dec('1399'), hex2dec('6726'), hex2dec('76af'), hex2dec('4434'), hex2dec('55bd'),
            hex2dec('ad4a'), hex2dec('bcc3'), hex2dec('8e58'), hex2dec('9fd1'), hex2dec('eb6e'), hex2dec('fae7'), hex2dec('c87c'), hex2dec('d9f5'),
            hex2dec('3183'), hex2dec('200a'), hex2dec('1291'), hex2dec('0318'), hex2dec('77a7'), hex2dec('662e'), hex2dec('54b5'), hex2dec('453c'),
            hex2dec('bdcb'), hex2dec('ac42'), hex2dec('9ed9'), hex2dec('8f50'), hex2dec('fbef'), hex2dec('ea66'), hex2dec('d8fd'), hex2dec('c974'),
            hex2dec('4204'), hex2dec('538d'), hex2dec('6116'), hex2dec('709f'), hex2dec('0420'), hex2dec('15a9'), hex2dec('2732'), hex2dec('36bb'),
            hex2dec('ce4c'), hex2dec('dfc5'), hex2dec('ed5e'), hex2dec('fcd7'), hex2dec('8868'), hex2dec('99e1'), hex2dec('ab7a'), hex2dec('baf3'),
            hex2dec('5285'), hex2dec('430c'), hex2dec('7197'), hex2dec('601e'), hex2dec('14a1'), hex2dec('0528'), hex2dec('37b3'), hex2dec('263a'),
            hex2dec('decd'), hex2dec('cf44'), hex2dec('fddf'), hex2dec('ec56'), hex2dec('98e9'), hex2dec('8960'), hex2dec('bbfb'), hex2dec('aa72'),
            hex2dec('6306'), hex2dec('728f'), hex2dec('4014'), hex2dec('519d'), hex2dec('2522'), hex2dec('34ab'), hex2dec('0630'), hex2dec('17b9'),
            hex2dec('ef4e'), hex2dec('fec7'), hex2dec('cc5c'), hex2dec('ddd5'), hex2dec('a96a'), hex2dec('b8e3'), hex2dec('8a78'), hex2dec('9bf1'),
            hex2dec('7387'), hex2dec('620e'), hex2dec('5095'), hex2dec('411c'), hex2dec('35a3'), hex2dec('242a'), hex2dec('16b1'), hex2dec('0738'),
            hex2dec('ffcf'), hex2dec('ee46'), hex2dec('dcdd'), hex2dec('cd54'), hex2dec('b9eb'), hex2dec('a862'), hex2dec('9af9'), hex2dec('8b70'),
            hex2dec('8408'), hex2dec('9581'), hex2dec('a71a'), hex2dec('b693'), hex2dec('c22c'), hex2dec('d3a5'), hex2dec('e13e'), hex2dec('f0b7'),
            hex2dec('0840'), hex2dec('19c9'), hex2dec('2b52'), hex2dec('3adb'), hex2dec('4e64'), hex2dec('5fed'), hex2dec('6d76'), hex2dec('7cff'),
            hex2dec('9489'), hex2dec('8500'), hex2dec('b79b'), hex2dec('a612'), hex2dec('d2ad'), hex2dec('c324'), hex2dec('f1bf'), hex2dec('e036'),
            hex2dec('18c1'), hex2dec('0948'), hex2dec('3bd3'), hex2dec('2a5a'), hex2dec('5ee5'), hex2dec('4f6c'), hex2dec('7df7'), hex2dec('6c7e'),
            hex2dec('a50a'), hex2dec('b483'), hex2dec('8618'), hex2dec('9791'), hex2dec('e32e'), hex2dec('f2a7'), hex2dec('c03c'), hex2dec('d1b5'),
            hex2dec('2942'), hex2dec('38cb'), hex2dec('0a50'), hex2dec('1bd9'), hex2dec('6f66'), hex2dec('7eef'), hex2dec('4c74'), hex2dec('5dfd'),
            hex2dec('b58b'), hex2dec('a402'), hex2dec('9699'), hex2dec('8710'), hex2dec('f3af'), hex2dec('e226'), hex2dec('d0bd'), hex2dec('c134'),
            hex2dec('39c3'), hex2dec('284a'), hex2dec('1ad1'), hex2dec('0b58'), hex2dec('7fe7'), hex2dec('6e6e'), hex2dec('5cf5'), hex2dec('4d7c'),
            hex2dec('c60c'), hex2dec('d785'), hex2dec('e51e'), hex2dec('f497'), hex2dec('8028'), hex2dec('91a1'), hex2dec('a33a'), hex2dec('b2b3'),
            hex2dec('4a44'), hex2dec('5bcd'), hex2dec('6956'), hex2dec('78df'), hex2dec('0c60'), hex2dec('1de9'), hex2dec('2f72'), hex2dec('3efb'),
            hex2dec('d68d'), hex2dec('c704'), hex2dec('f59f'), hex2dec('e416'), hex2dec('90a9'), hex2dec('8120'), hex2dec('b3bb'), hex2dec('a232'),
            hex2dec('5ac5'), hex2dec('4b4c'), hex2dec('79d7'), hex2dec('685e'), hex2dec('1ce1'), hex2dec('0d68'), hex2dec('3ff3'), hex2dec('2e7a'),
            hex2dec('e70e'), hex2dec('f687'), hex2dec('c41c'), hex2dec('d595'), hex2dec('a12a'), hex2dec('b0a3'), hex2dec('8238'), hex2dec('93b1'),
            hex2dec('6b46'), hex2dec('7acf'), hex2dec('4854'), hex2dec('59dd'), hex2dec('2d62'), hex2dec('3ceb'), hex2dec('0e70'), hex2dec('1ff9'),
            hex2dec('f78f'), hex2dec('e606'), hex2dec('d49d'), hex2dec('c514'), hex2dec('b1ab'), hex2dec('a022'), hex2dec('92b9'), hex2dec('8330'),
            hex2dec('7bc7'), hex2dec('6a4e'), hex2dec('58d5'), hex2dec('495c'), hex2dec('3de3'), hex2dec('2c6a'), hex2dec('1ef1'), hex2dec('0f78')
        ]    
    end
    
    methods
        % 构造函数
        function obj = HDLCParser()
            obj.reset();
        end
        
        % 重置解析器状态
        function reset(obj)
            obj.buffer = [];
            obj.escaping = false;
            obj.receiving = false;
            obj.crc = obj.HDLC_CRCINIT;
        end
        
        % CRC校验
        function valid = check_crc(obj)
            valid = (obj.crc == obj.HDLC_CRCGOOD);
        end
        
        % CRC计算迭代
        function new_crc = crc_iteration(obj, crc, byte)
            % 确保byte在0-255范围内
            byte = bitand(byte, 255);
            % 计算索引
            index = bitxor(bitand(crc, 255), byte) + 1; % MATLAB索引从1开始
            % 确保索引在有效范围内
            if index < 1 || index > 256
                error('CRC索引超出范围');
            end
            % 计算新的CRC值
            new_crc = bitxor(bitshift(crc, -8), obj.fcstab(index));
        end
        
        % 字节解析
        function data = parse_byte(obj, byte)
            data = [];
            
            if byte == obj.HDLC_FLAG
                if obj.receiving
                    if length(obj.buffer) >= 2
                        if obj.check_crc()
                            data = obj.buffer(1:end-2);
                        end
                    end
                    obj.reset();
                else
                    obj.receiving = true;
                    obj.crc = obj.HDLC_CRCINIT;
                end
            elseif obj.receiving
                if byte == obj.HDLC_ESCAPE
                    obj.escaping = true;
                else
                    if obj.escaping
                        byte = bitxor(byte, obj.HDLC_ESCAPE_MASK);
                        obj.escaping = false;
                    end
                    
                    obj.crc = obj.crc_iteration(obj.crc, byte);
                    
                    if length(obj.buffer) < 1024
                        obj.buffer = [obj.buffer byte];
                    else
                        obj.reset();
                    end
                end
            end
        end
    end
end