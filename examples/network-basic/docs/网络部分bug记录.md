### 1. man不能正常工作

#### 复现

在Ubuntu18.0LTS上使用`man arp`

> man: command exited with status 159: (cd /usr/share/man && /usr/lib/man-db/zsoelim) | (cd /usr/share/man && /usr/lib/man-db/manconv -f UTF-8:ISO-8859-1 -t UTF-8//IGNORE) | (cd /usr/share/man && preconv -e UTF-8) | (cd /usr/share/man && tbl) | (cd /usr/share/man && nroff -mandoc -rLL=159n -rLT=159n -Tutf8)



#### 解决

搜索Google发现是Ubuntu18.0LTS常规问题？

`export MAN_DISABLE_SECCOMP=1`可以解决



#### 技术拓展

- SECCOMP是什么？



### 2. `sudo source: commond not found`

source是shell的built-in command，非program



### 3. telnet: Unable to connect to remote host: Connection refused

- 直接搜索报错。。。好像是因为没装telnetd
- https://stackoverflow.com/questions/34389620/telnet-unable-to-connect-to-remote-host-connection-refused



### 4. emmm，配环境

- 讲道理，应该是有1个NAT连外网、1个Host-Only构建内网（宿主虚拟机间和虚拟机虚拟机间相互可ping）就可以了？

- https://ubuntu.com/server/docs/network-configuration
- https://blog.csdn.net/itnerd/article/details/107108262



### 5. 通过squid代理无法wget baidu

#### 复现

```shell
# 修改squid配置，在文件最后加入两行，保存退出
divsigma@tom:~$ vi /etc/squid/squid.conf

acl localnet src 192.168.56.0/24
http_access allow localnet

# 重启服务
divsigma@tom:~$ sudo service squid restart

# 通过代理访问
divsigma@jerry:~$ wget -e use_proxy=on --header="Connection: close" http://www.baidu.com/index.html
--2022-03-19 05:38:46--  http://www.baidu.com/index.html
Resolving tom (tom)... 192.168.56.2
Connecting to tom (tom)|192.168.56.2|:3128... connected.
Proxy request sent, awaiting response... 403 Forbidden
2022-03-19 05:38:46 ERROR 403: Forbidden.
```



#### 解决

- 尝试重启tom，不行

- 尝试对比教材的tcpdump结果和403的jerry抓包结果，似乎tom并没有向baidu发起请求

  - 教材的tcpdump是在代理机上抓内网网卡包的，应该对比403的tom内网网卡抓包结果。看到tom建立连接后发了几个PSH，然后主动发送了FIN报文。

  ```shell
  divsigma@tom:~$ sudo tcpdump -i enp0s8 -nt '((dst 192.168.56.2) or (src 192.168.56.2)) and not (port 22)'
  IP 192.168.56.3.48372 > 192.168.56.2.3128: Flags [S], length 0
  IP 192.168.56.2.3128 > 192.168.56.3.48372: Flags [S.], length 0
  IP 192.168.56.3.48372 > 192.168.56.2.3128: Flags [.], length 0
  IP 192.168.56.3.48372 > 192.168.56.2.3128: Flags [P.], length 195
  IP 192.168.56.2.3128 > 192.168.56.3.48372: Flags [.], length 0
  IP 192.168.56.2.3128 > 192.168.56.3.48372: Flags [P.], length 3953
  IP 192.168.56.2.3128 > 192.168.56.3.48372: Flags [F.], length 0
  IP 192.168.56.3.48372 > 192.168.56.2.3128: Flags [.], length 0
  IP 192.168.56.3.48372 > 192.168.56.2.3128: Flags [R.], length 0
  ```

  - 根据上述，怀疑是tom（代理）拒绝了jerry请求，尝试去确认配置文件/etc/squid/squid.conf，发现这个**文件就是个docs，阅读**其中`TAG: acl`和`TAG: http_access`
  - **RTFM**：从`TAG: http_access`中推测，`http_access`是按顺序匹配起作用的，如果ACL list没有匹配到任何一个`http_access`，默认是允许的，所以为了安全，文件中有一行`http_access deny all`用于禁止ACL list未定义的行为。猜测deny all之后的匹配都忽略了，而我的两行配置是在文件最后加的。

- 验证猜想：文件中已经自带`http_access allow localnet`，所以只需要在前面添加`acl localnet src 192.168.56.0/24`。重启squid，tom处理了jerry的请求！



#### 技术拓展

