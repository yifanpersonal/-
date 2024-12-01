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
SOCKET ServerSocket;
// 地址
SOCKADDR_IN ServerAddress;
SOCKADDR_IN ClientAddress;
int ServerAddLen;
int ClientAddLen;
WSADATA wsaData;
double Loss = 0.2;
int timer = 20;
string savePath;


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
    int num;
    //uint16_t pckNum;
    char data[DATA_LEN_MAX];
};

void reset(Packet &packet)
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
    char data[DATA_LEN_MAX] = { 0 };
}

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
    //sum += packet.pckNum;

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

void init() {
    // 初始化套接字
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    //ioctlsocket(ClientSocket, FIONBIO, &unblockmode);

    // 设置服务器端地址
    ServerAddress.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &(ServerAddress.sin_addr));
    ServerAddress.sin_port = htons(8888);

    // 设置客户端地址
    ClientAddress.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &(ServerAddress.sin_addr));
    ClientAddress.sin_port = htons(80);

    // 绑定服务端
    ServerSocket = socket(AF_INET, SOCK_DGRAM, 0);
    int bind_res = bind(ServerSocket, (sockaddr*)&ServerAddress, sizeof(ServerAddress));
    if (bind_res == SOCKET_ERROR) {
        cout << "server: bind failed." << endl;
    }

    unsigned long on = 1;
    ioctlsocket(ServerSocket, FIONBIO, &on);

    ClientAddLen = sizeof(ClientAddress);
    ServerAddLen = sizeof(ServerAddress);

    cout << "服务器端初始化完成！" << endl;
}

// 三次握手建立连接
int ServerConnect() {
    // 1，3服务器接收，2服务器发送
    Packet packet;
    reset(packet);
    // 两个缓冲区
    char* RecvBuff = new char[sizeof(Packet)];
    char* SendBuff = new char[sizeof(Packet)];
    cout << "服务器开始等待连接......" << endl;

    /**** 第一次握手 ****/
    cout << "[server]:等待第一次握手..." << endl;
    while (true) {
        // 通过recvfrom函数接收报文
        int recv_len = recvfrom(ServerSocket, RecvBuff, sizeof(Packet), 0, (sockaddr*)&ClientAddress, &ClientAddLen);
        if (recv_len < 0) {
            continue;
        }

        memcpy(&packet, RecvBuff, sizeof(Packet));

        // 简单检查是否符合第一次握手预期
        if (packet.syn == 1) {
            cout << "[server]:第一次握手消息接收成功！" << endl;
            break;
        }
        else
        {
            cout << "第一次握手消息不符合预期！" << endl;
            return -1;
        }
    }

    /**** 发起第二次握手 ****/
    // 创建packet（修改信息）
    reset(packet);
    packet.syn = 1;
    packet.ack = 1;
    packet.Checksum = Checksum(packet);

    memcpy(SendBuff, &packet, sizeof(Packet));

    if (sendto(ServerSocket, SendBuff, sizeof(Packet), 0, (sockaddr*)&ClientAddress, ClientAddLen) == -1) {
        cout << "第二次握手发送数据失败！" << endl;
        return -1;
    }
    cout << "[server]:第二次握手消息发送成功！" << endl;

    /**** 接收第三次握手 ****/
    while (true) {
        // 通过recvfrom函数接收报文
        int recv_len = recvfrom(ServerSocket, RecvBuff, sizeof(Packet), 0, (sockaddr*)&ClientAddress, &ClientAddLen);
        if (recv_len < 0) {
            continue;
        }

        memcpy(&packet, RecvBuff, sizeof(Packet));

        // 简单检查是否符合第三次握手预期
        if (packet.ack == 1) {
            cout << "[server]:第三次握手消息接收成功！" << endl;
            break;
        }
    }

    // 握手成功后
    cout << "[server]:等待接收数据" << endl;
    return 1;
}

void RecvMessage() {
    Packet packet;
    // 一开始seq和ack等相关字段可根据实际情况初始化，这里暂不做特殊初始化
    char* RecvBuff = new char[sizeof(Packet)+1];
    char* SendBuff = new char[sizeof(Packet)+1];

    // 用于存储接收到的所有数据
    char* RecvData = new char[DATA_SIZE];
    unsigned long long int ByteNum = 0;
    int pckNum = 0;

    // 通过while循环实现对分组的接收
    int groupnum = 0;
    while (true) {
        // 一进循环首先接收
        reset(packet);
        int recv_len = recvfrom(ServerSocket, RecvBuff, sizeof(Packet), 0, (sockaddr*)&ClientAddress, &ClientAddLen);
        if (recv_len < 0)
            continue;

        // 报文长度不为0时，将接收到的数据包赋值给packet，并进行简单检查
        memcpy(&packet, RecvBuff, sizeof(Packet));

        // 检查是否符合预期接收条件
        if (packet.seq != groupnum || packet.Checksum != Checksum(packet)) {
            continue;
        }
        cout << "[server]:分组" << groupnum << "发来的数据报[" << pckNum++ << "]接收成功！" << endl;

        // 先检查一下是不是over
        if (packet.over == 1) {
            cout << "[server]:开始解析发来的全部数据包" << endl;
            string path = "E:\\计算机网络\\作业\\lab3测试\\测试文件\\" + savePath;
            // 输出流
            ofstream os(path.c_str(), ofstream::binary);
            for (int i = 0; i < ByteNum; i++)
            {
                os << RecvData[i];
            }
            os.close();
            return;
        }
        else {
            // 正常保存数据到RecvData就行，从ByteNum开始存
            memcpy(RecvData + ByteNum, packet.data,  packet.num);
            ByteNum += packet.num; // 保留数据
        }

        // 接收端延时）
        Sleep(timer);
        reset(packet);
        packet.ack = groupnum; // 确认收到分组
        packet.over = 0;
        packet.Checksum = Checksum(packet); 

        memcpy(SendBuff, &packet, sizeof(Packet));
        while (sendto(ServerSocket, SendBuff, sizeof(Packet), 0, (sockaddr*)&ClientAddress, ClientAddLen) < 0);

        // 等待下一次
        groupnum = (groupnum + 1) % 2; // 0/1置换
        cout << "[server]:等待分组" << groupnum << "发来的数据......" << endl;
    }

    // 释放动态分配的内存
    delete[] RecvBuff;
    delete[] SendBuff;
    delete[] RecvData;
}

// 四次挥手的接收方，关闭连接
void ServerCloseConnection() {
    Packet packet;
    reset(packet);
    char* RecvBuff = new char[sizeof(Packet)];
    char* SendBuff = new char[sizeof(Packet)];

    /**** 等待第一次挥手请求 ****/
    while (true) {
        int recv_len = recvfrom(ServerSocket, RecvBuff, sizeof(Packet), 0, (sockaddr*)&ClientAddress, &ClientAddLen);
        if (recv_len < 0) {
            continue;
        }

        // 接收到消息后，给packet赋值并读取
        memcpy(&packet, RecvBuff, sizeof(Packet));

        // 简单检查是否符合第一次挥手预期（仅检查关键字段）
        if (packet.fin == 1) {
            cout << "[server]:第一次挥手消息接收成功！" << endl;
            break;
        }
        else
        {
            cout << "第二次挥手消息不符合预期！" << endl;
            return;
        }
    }

    /**** 发起第二次挥手 ****/
    // 先修改packet的信息
    reset(packet);  
    packet.ack = 1;
    packet.Checksum = Checksum(packet); 

    // 装进发送缓冲区
    memcpy(SendBuff, &packet, sizeof(Packet));

    // 开始发送
    if (sendto(ServerSocket, SendBuff, sizeof(Packet), 0, (SOCKADDR*)&ClientAddress, ClientAddLen) < 0) {
        cout << "第二次挥手消息发送失败！" << endl;
        return;
    }
    cout << "[server]:第二次挥手消息发送成功！" << endl;

    /**** 发送第三次挥手消息 ****/
    // 先修改packet
    reset(packet);
    packet.fin = 1;
    packet.Checksum = Checksum(packet);

    // 装进发送缓冲区
    memcpy(SendBuff, &packet, sizeof(Packet));

    // 开始发送
    if (sendto(ServerSocket, SendBuff, sizeof(Packet), 0, (SOCKADDR*)&ClientAddress, ClientAddLen) < 0) {
        cout << "第三次挥手消息发送失败！" << endl;
        return;
    }
    cout << "[server]:第三次挥手消息发送成功！" << endl;

    /**** 接收第四次挥手 ****/
    reset(packet);
    while (true) {
        int recv_len = recvfrom(ServerSocket, RecvBuff, sizeof(Packet), 0, (SOCKADDR*)&ClientAddress, &ClientAddLen);
        if (recv_len < 0) {
            continue;
        }

        // 接收到消息后，给packet赋值并读取
        memcpy(&packet, RecvBuff, sizeof(Packet));

        // 简单检查是否符合第四次挥手预期（仅检查关键字段）
        if (packet.ack == 1) {
            cout << "[server]:第四次挥手消息接收成功！" << endl;
            break;
        }
    }

    // 握手结束
    cout << "服务器端正常结束并退出" << endl;
    Sleep(timer);

    // 释放动态分配的内存
    delete[] RecvBuff;
    delete[] SendBuff;
}



// 服务器端的主函数
int main() {
    // 初始化
    init();
    // 三次握手建立连接
    if (ServerConnect() == -1) {
        cout << "【Warning:】连接建立失败，服务器即将关闭！" << endl;
        Sleep(50);
        return -1;
    }

    cout << "输入延时" << endl;
    cin >> timer;

    cout << "输入保存文件名字或exit以退出接收" << endl;
    cin >> savePath;
    while (savePath != "exit")
    {
        // 数据传输
        RecvMessage();
        cout << "输入保存文件名字或exit以退出接收" << endl;
        cin >> savePath;
    }

    // 四次挥手结束连接
    ServerCloseConnection();

    return 0;

}
