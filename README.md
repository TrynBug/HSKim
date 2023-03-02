
### 김형섭 포트폴리오 프로젝트 소스코드

#### 01. MMO 게임 서버
<text>
&nbsp;&nbsp;- ChatServer
<br>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;<sub>IOCP 네트워크 라이브러리 사용</sub>
<br>&nbsp;&nbsp;- LoginServer
<br>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;<sub>IOCP 네트워크 라이브러리 사용</sub>
<br>&nbsp;&nbsp;- GameServer
<br>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;<sub>게임 컨텐츠 전용 IOCP 네트워크 라이브러리 사용</sub>
<br>&nbsp;&nbsp;- MonitoringServer
<br>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;<sub>IOCP 네트워크 라이브러리 사용</sub>
<br><sub>(주의: 실행하기 위해서는 redis 서버, MySQL DB가 연동되어 있어야 하기 때문에 소스코드만으로는 정상적으로 실행되지 않습니다.)</sub>
</text>

#### 02. IOCP 네트워크 라이브러리
<text>&nbsp;&nbsp;- 정적 라이브러리</text>
#### 03. 게임 컨텐츠 전용 IOCP 네트워크 라이브러리
<text>&nbsp;&nbsp;- 정적 라이브러리</text>
#### 04. Lockfree Stack
#### 05. Lockfree Queue
#### 06. TLS 메모리풀
#### 07. Red-Black Tree
#### 08-09. 길찾기 프로그램
<text>
&nbsp;&nbsp;- A* 알고리즘 길찾기 기능
<br>&nbsp;&nbsp;- Jump Point Search 알고리즘 길찾기 기능
</text>
