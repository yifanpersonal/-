#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1

#include <WinSock2.h>
#include <thread>
#include <fstream>
#include <cmath>
#include<time.h>
#include<fstream>
#include<iostream>
#include<windows.h>
#include <ws2tcpip.h>

using namespace std;
#pragma comment(lib,"ws2_32.lib")
#define WAITING_MAX 100
#define DATA_LEN_MAX 15000
#define DATA_SIZE 50000000

/**** 一些常量定义****/
const uint16_t SOURCE_PORT = 127001;
const uint16_t DESTINATION_PORT = 127001;
const uint32_t SOURCE_IP = 80;
const uint32_t DESTINATION_IP = 8888;

/**** 全局变量定义****/
// 套接字
SOCKET ClientSocket;
// 地址
SOCKADDR_IN ServerAddress;
SOCKADDR_IN ClientAddress;
int ServerAddLen;
int ClientAddLen;
WSADATA wsaData;
double Loss;
string figName;


// 存储data的数组（char*类型，和缓冲区一样）
char* SendData;

// 数据包格式
struct Packet {
    uint16_t SourcePort; // 源端口号
    uint16_t DestPort; // 目的端口号
    uint32_t SourceIp; // 源IP地址
    uint32_t DestIp; // 目的IP地址
    uint16_t Checksum; // 校验和
    uint16_t len; // 长度
    int ack; // 确认号
    int syn;
    int seq; // 序列号
    int fin;
    int over;
    int num;//穿了多少字节
    char data[DATA_LEN_MAX];
};

// 计算给定的数据头的校验和
uint16_t Checksum(Packet packet) {
    uint32_t sum = 0;

    sum += packet.SourcePort;
    sum += packet.DestPort;
    // 将源IP地址拆分成16位分别计算
    sum += (packet.SourceIp >> 16) & 0xFFFF;
    sum += packet.SourceIp & 0xFFFF;
    // 将目的IP地址拆分成16位分别计算
    sum += (packet.DestIp >> 16) & 0xFFFF;
    sum += packet.DestIp & 0xFFFF;
    sum += packet.len;

    // 以16位为单位计算数据部分的校验和
    for (size_t i = 0; i < sizeof(packet.data); i += 2) {
        if (i + 1 < sizeof(packet.data)) {
            // 一次处理两个字节（16位）
            sum += static_cast<uint32_t>(packet.data[i]) << 8 | static_cast<uint32_t>(packet.data[i + 1]);
        }
        else {
            // 如果剩余不足两个字节，单独处理最后一个字节
            sum += static_cast<uint32_t>(packet.data[i]);
        }
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return static_cast<uint16_t>(sum);
}

//数据包初始化函数
void reset(Packet& packet)
{
    packet.SourcePort = SOURCE_PORT;
    packet.DestPort = DESTINATION_PORT;
    packet.SourceIp = SOURCE_IP;
    packet.DestIp = DESTINATION_IP;
    packet.Checksum = 0;
    packet.len = 0;
    packet.ack = 0;
    packet.syn = 0;
    packet.seq = 0; // 序列号
    packet.fin = 0;
    packet.over = 0;
    char data[DATA_LEN_MAX];
}

void init() {
    // 初始化套接字
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // 设置服务器端地址
    ServerAddress.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &(ServerAddress.sin_addr));
    ServerAddress.sin_port = htons(8888);

    // 设置客户端地址
    ClientAddress.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &(ServerAddress.sin_addr));
    ClientAddress.sin_port = htons(80);

    // 绑定客户端
    ClientSocket = socket(AF_INET, SOCK_DGRAM, 0);
    int bind_res = bind(ClientSocket, (sockaddr*)&ClientAddress, sizeof(ClientAddress));
    if (bind_res == SOCKET_ERROR) {
        cout << "client: bind failed." << endl;
    }

    unsigned long on = 1;
    ioctlsocket(ClientSocket, FIONBIO, &on);

    ClientAddLen = sizeof(ClientAddress);
    ServerAddLen = sizeof(ServerAddress);

    cout << "客户端端初始化完成！" << endl;

}

int ClientConnect() {
    // 主要完成发起1、3请求

    /**** 发起第一次握手请求 ****/
    // 创建数据包并设置相关字段
    Packet packet;
    reset(packet);
    packet.syn = 1;
    packet.len = sizeof(packet);

    // 创建发送缓冲区并将数据包复制进去
    char SendBuff[sizeof(Packet)];
    memcpy(SendBuff, &packet, sizeof(Packet));

    // 发送第一次握手请求
    if (sendto(ClientSocket, SendBuff, sizeof(Packet), 0, (SOCKADDR*)&ServerAddress, ServerAddLen) == -1) {
        cout << "第一次握手发送数据失败！" << endl;
        return -1;
    }
    cout << "[client]:第一次握手发送数据成功！" << endl;

    /**** 接收第二次握手消息 ****/
    // 创建接收缓冲区和用于存储接收到的数据包的变量
    char RecvBuff[sizeof(Packet)];

    // 接收第二次握手消息
    while (true)
    {
        int recv_len = recvfrom(ClientSocket, RecvBuff, sizeof(Packet), 0, (SOCKADDR*)&ServerAddress, &ServerAddLen);
        if (recv_len < 0) {
            continue;
        }
        memcpy(&packet, RecvBuff, sizeof(Packet));

        // 检查接收到的数据包是否符合第二次握手消息的预期
        if (packet.syn == 1 && packet.ack == 1) {
            cout << "[client]:第二次握手消息接收成功！" << endl;
            break;
        }
        else {
            cout << "第二次握手消息不符合预期！" << endl;
            return -1;
        }
    }

    /**** 发起第三次握手 ****/
    // 创建数据包并设置相关字段
    reset(packet);
    packet.ack = 1; // 确认接收到第二次握手消息
    packet.len = sizeof(packet);
    packet.Checksum = Checksum(packet);

    memcpy(SendBuff, &packet, sizeof(Packet));

    // 发送第三次握手请求
    if (sendto(ClientSocket, SendBuff, sizeof(Packet), 0, (SOCKADDR*)&ServerAddress, ServerAddLen) == -1) {
        cout << "第三次握手发送数据失败！" << endl;
        return -1;
    }

    cout << "[client]:第三次握手成功！" << endl;
    cout << "[client]:开始传输数据" << endl;

    return 0;
}

void SendMessage() {
    // 先建立数据头部和缓冲区
    Packet packet;
    char* RecvBuff = new char[sizeof(Packet)+1];
    char* SendBuff = new char[sizeof(Packet)+1];
    SendData = new char[DATA_SIZE];
    //packet.pckNum = 0;

    /**** 加载数据 ****/

    // 打开文件
    ifstream f("E:\\计算机网络\\作业\\lab3测试\\测试文件\\" + figName, ifstream::binary);
    // 和 server 端不同的是不知道文件大小，只能一个一个读入
    long long int ByteNum = 0;
    char temp = f.get();

    while (f) {
        SendData[ByteNum++] = temp;
        temp = f.get();
    }

    cout << "[client]:文件已经成功读入，大小为" << ByteNum << "字节" << endl;
    f.close();

    /**** 传输数据 ****/
    int allpacknum = ByteNum / DATA_LEN_MAX + 1;//算上结束的数据包一共需要传多少个
    cout << "[client]:文件即将拆分成" << allpacknum << "个包进行传输" << endl;
    int groupnum = 0;  // 分组 0
    long long int TempByte = 0;  // 已经传输过去的字节数量
    int TempTally = 0;  // 已经传过去的包数
    int loss_pck = 0;  //已经丢包的个数
    int count_pck = allpacknum * Loss; //设置预期丢包数
    int count = allpacknum / count_pck; //计算每传几个包丢一个
    clock_t time;
    clock_t begin = clock(); //开始计时

    while (true) 
    {
        // 初始化数据包
        reset(packet);
        packet.seq = groupnum;
        packet.len = sizeof(packet);
        int Templen;

        // 最后一次发剩下的
        if (TempTally == allpacknum - 1)
        {
            Templen = ByteNum - TempByte;
        }
        else
        {
            Templen = DATA_LEN_MAX;
        }
        packet.num = Templen;

        if (TempTally == allpacknum - 1)
        {
            // 最后一次传输，确保只复制剩余的数据            
            memcpy(packet.data, SendData + TempByte, ByteNum - TempByte);
        }
        else 
        {
            memcpy(packet.data, SendData + TempByte, Templen);
        }
        packet.Checksum = Checksum(packet);

        memcpy(SendBuff, &packet, sizeof(Packet));

        // 每 10 个丢个包
        if (TempTally % count == 1) {
            cout << "[client]:分组" << groupnum << "丢包[" << TempTally << "]测试......" << endl;
            time = clock();
        }
        else if (TempTally == 0)
        {
            //第一次发送的单独处理
            while (sendto(ClientSocket, SendBuff, sizeof(packet), 0, (SOCKADDR*)&ServerAddress, ServerAddLen) < 0)
                ;
            while (1)
            {
                int recv_len = recvfrom(ClientSocket, RecvBuff, sizeof(Packet), 0, (SOCKADDR*)&ServerAddress, &ServerAddLen);
                if (recv_len < 0)
                    continue;
                // 接收到了进行确认
                Packet receivedPacket;
                memcpy(&receivedPacket, RecvBuff, sizeof(Packet));
                if (receivedPacket.ack == groupnum) {
                    // 确认了再给 TempTally++
                    cout << "[client]:已确认分组" << groupnum << "的数据包[" << TempTally++ << "]发送成功！" << endl;
                    TempByte += Templen;
                    cout << "tempBtye" << TempByte << endl;
                    groupnum = (groupnum + 1) % 2;
                    break;
                }                
            }
            continue;
        }
        else {  // 正常发
            while (sendto(ClientSocket, SendBuff, sizeof(packet) , 0, (SOCKADDR*)&ServerAddress, ServerAddLen) < 0); 
            cout << "[client]:尝试发送分组" << groupnum << "的数据包[" << TempTally << "]！" << endl;
            
            time = clock();
        }

        // 然后等待回应
        // 发完就开始计时
        while (true) {
            if (clock() - time > WAITING_MAX) {
                cout << "[client]:分组" << groupnum << "的数据包[" << TempTally << "]超时，正在重新发送......" << endl;
                loss_pck++;  // 丢包数++
                while (sendto(ClientSocket, SendBuff, sizeof(packet), 0, (SOCKADDR*)&ServerAddress, ServerAddLen) < 0);
                cout << "[client]:重新发送分组" << groupnum << "的数据包[" << TempTally << "]！" << endl;
          
                // 重置计时器
                time = clock();//接收端有延迟，如果不重置会多次发送同一数据包，进入下一次超时重传判断
            }

            int recv_len = recvfrom(ClientSocket, RecvBuff, sizeof(Packet), 0, (SOCKADDR*)&ServerAddress, &ServerAddLen);

            if (recv_len < 0)
                continue;

            // 接收到了进行确认
            Packet receivedPacket;
            memcpy(&receivedPacket, RecvBuff, sizeof(Packet));
            if (receivedPacket.ack == groupnum) {
                // 确认了再给 TempTally++
                cout << "[client]:已确认分组" << groupnum << "的数据包[" << TempTally++ << "]发送成功！" << endl;
                TempByte += Templen;
                cout << "tempBtye" << TempByte << endl;
                break;
            }
            else
            {
                while (sendto(ClientSocket, SendBuff, sizeof(packet), 0, (SOCKADDR*)&ServerAddress, ServerAddLen) < 0);
                cout << "[client]:重新发送分组" << groupnum << "的数据包[" << TempTally << "]！" << endl;
            }
        }

        // 然后进行下一个
        if (TempTally >= allpacknum) {
            cout << "[client]:全部数据包发送完毕......" << endl;
            break;
        }

        // 修改信息
        groupnum = (groupnum + 1) % 2;
    }

    // 发送结束的报文 OVER=1
    reset(packet);
    packet.seq = (groupnum + 1) % 2;  // 还是需要下一个分组来发
    groupnum = (groupnum + 1) % 2;
    packet.over = 1;
    packet.len = sizeof(packet);
    packet.Checksum = Checksum(packet);

    memcpy(SendBuff, &packet, sizeof(Packet));
    while (sendto(ClientSocket, SendBuff, sizeof(Packet), 0, (SOCKADDR*)&ServerAddress, ServerAddLen) < 0)
        ;
    cout << "[client]:尝试发送分组" << groupnum << "的数据包[" << TempTally << "]！" << endl;
    cout << "此数据包为结束数据包，全部数据包已经发送" << endl;

    // 发送端计算用时和吞吐率
    clock_t total = clock() - begin;
    double seed = (double)ByteNum / total;
    //double loss = (double)loss_pck / allpacknum;
    cout << "/************************************************************/" << endl;
    cout << "[client]:本次传输共发送" << ByteNum << "个字节，" << allpacknum << "个数据包" << endl;
    cout << "耗时:" << total << "ms" << endl;
    cout << "吞吐率:" << seed << " Byte / ms" << endl;

    cout << "[client]:传输关闭" << endl;

    // 释放动态分配的内存
    delete[] RecvBuff;
    delete[] SendBuff;
}

void ClientCloseConnection() {
    // 发起1和4，接收2和3

    /**** 发起第一次挥手请求 ****/
    // 创建数据包并设置相关字段
    Packet packet;
    reset(packet);
    packet.fin = 1;
    packet.len = sizeof(packet);
    packet.Checksum = Checksum(packet);
    char* RecvBuff = new char[sizeof(Packet)];
    char* SendBuff = new char[sizeof(Packet)];

    memcpy(SendBuff, &packet, sizeof(Packet));

    // 发送第一次挥手请求
    if (sendto(ClientSocket, SendBuff, sizeof(Packet), 0, (SOCKADDR*)&ServerAddress, ServerAddLen) == -1) {
        cout << "第一次挥手发送数据失败！" << endl;
        return;
    }
    cout << "[client]:第一次挥手消息发送成功！" << endl;
    reset(packet);

    /**** 接收第二次挥手请求 ****/

    // 接收第二次挥手请求
    while (true)
    {
        int recv_len = recvfrom(ClientSocket, RecvBuff, sizeof(Packet), 0, (SOCKADDR*)&ServerAddress, &ServerAddLen);
        if (recv_len < 0) {
            continue;
        }
        memcpy(&packet, RecvBuff, sizeof(Packet));

        // 检查接收到的数据包是否符合第二次挥手消息的预期
        if (packet.ack == 1) {
            cout << "[client]:第二次挥手消息接收成功！" << endl;
            break;
        }
        else {
            cout << "第二次挥手消息不符合预期！" << endl;
            return;
        }
    }
    reset(packet);

    /**** 接收第三次挥手请求 ****/
    // 创建用于存储接收到的数据包的变量

    while (true)
    {
        // 接收第三次挥手请求
        int recvLen = recvfrom(ClientSocket, RecvBuff, sizeof(Packet), 0, (SOCKADDR*)&ServerAddress, &ServerAddLen);
        if (recvLen < 0) {
            continue;
        }
        memcpy(&packet, RecvBuff, sizeof(Packet));

        // 检查接收到的数据包是否符合第三次挥手消息的预期
        if (packet.fin == 1 && packet.ack == 0) {
            cout << "[client]:第三次挥手消息接收成功！" << endl;
            break;
        }
        else {
            cout << "第三次挥手消息不符合预期！" << endl;
            return;
        }
    }

    /**** 发起第四次挥手请求 ****/
    // 创建数据包并设置相关字段
    reset(packet);
    packet.ack = 1;
    packet.len = sizeof(packet);
    packet.Checksum = Checksum(packet);

    memcpy(SendBuff, &packet, sizeof(Packet));

    // 发送第四次挥手请求
    if (sendto(ClientSocket, SendBuff, sizeof(Packet), 0, (SOCKADDR*)&ServerAddress, ServerAddLen) == -1) {
        cout << "第四次挥手发送数据失败！" << endl;
        return;
    }
    cout << "[client]:第四次挥手消息发送成功！" << endl;
    cout << "客户端正常结束并退出" << endl;
}


int main()
{
    init();

    cout << "客户端发起连接建立请求..." << endl;
    if (ClientConnect() == -1) {
        cout << "建立连接失败！" << endl;
    }

    cout << "请输入丢包率：" << endl;
    cin >> Loss;

    cout << "/****** 输入需要传输的文件名字或exit以退出传输 ******/" << endl;
    cin >> figName;

    while (figName != "exit")
    {
        // 数据传输
        SendMessage();
        cout << "/****** 输入需要传输的文件名字或exit以退出传输 ******/" << endl;
        cin >> figName;
    }
    // 四次挥手结束连接
    ClientCloseConnection();

    return 0;

}
