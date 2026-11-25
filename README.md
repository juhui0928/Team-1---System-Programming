# Linux CyberVault
Linux 기반 실시간 파일 감시 및 원격 백업·복구 시스템

## 팀원
- 2025004569 김주희 (서버 / 네트워크)
- 2025008657 김하경 (데이터 처리 / 복구)
- 2025002614 정지우 (감시 / CLI)

## 프로젝트 개요

Linux CyberVault는 Linux 환경에서 파일 변경을 실시간으로
감시하고, 중요 데이터를 암호화하여 원격 서버에 백업하는
파일 보호 시스템이다.

이상 파일 변경이 감지되면 자동 백업을 중단하여 기존 정상
백업을 보호하며, 네트워크 연결이 끊어진 경우 자동 재연결을
통해 중단된 위치부터 전송을 재개한다.

## 주요 기능

- inotify 기반 실시간 파일 변경 감시
- SHA-256 기반 파일 무결성 검증
- AES-256-GCM 기반 파일 암호화 및 복호화
- TCP 소켓 기반 원격 백업 및 복구
- 백업 버전 관리 및 이전 버전 복구
- pthread 기반 멀티스레드 서버 및 작업 큐
- 의심 확장자, 파일 크기 급변, 대량 변경 탐지
- 이상 변경 발생 시 자동 백업 중단
- 사용자 명령을 통한 자동 백업 재개
- 서버 토큰 인증 및 암호화 키 관리
- 네트워크 자동 재연결 및 Resume
- fsync 및 하드링크 기반 안전한 버전 저장

## 시스템 구조

```text
Client
  |
  +-- File Monitor (inotify)
  |
  +-- Work Queue (pthread)
  |
  +-- Anomaly Detection
  |
  +-- SHA-256 / AES-256-GCM
  |
  +-- TCP Transfer / Resume
  |
  v
Server
  |
  +-- Token Authentication
  +-- Multithreaded Connection Handler
  +-- Upload Checkpoint
  +-- Version Storage
  +-- Backup / List / Restore
```

## 개발 환경

- Ubuntu Linux
- C11
- GCC
- POSIX API
- OpenSSL
- pthread
- TCP/IP Socket
- Git / GitHub

## 설치 및 빌드

```bash
sudo apt update
sudo apt install build-essential libssl-dev openssl
make clean
make
```

빌드 결과:

- client_app: 클라이언트 실행 파일
- server_app: 서버 실행 파일

## 초기 설정

프로젝트 디렉터리에서 실행한다.

```bash
./client_app keygen cybervault.key

export CYBERVAULT_KEY_FILE="$PWD/cybervault.key"
export CYBERVAULT_TOKEN=$(openssl rand -hex 32)
export CYBERVAULT_SERVER=127.0.0.1
```

서버와 클라이언트는 동일한 인증 토큰을 사용해야 한다.
복구 시에는 백업에 사용한 암호화 키가 필요하다.
암호화 키와 인증 토큰은 GitHub에 업로드하지 않는다.

## 서버 실행

```bash
./server_app
```

기본 TCP 포트는 9000이다.

## 클라이언트 명령어

별도 터미널에서 동일한 인증 토큰 및 키 경로를 설정한다.

| 명령어 | 기능 |
|---|---|
| ./client_app keygen keyfile | 암호화 키 생성 |
| ./client_app backup file.txt | 수동 원격 백업 |
| ./client_app list | 백업 목록 조회 |
| ./client_app list file.txt | 파일별 버전 조회 |
| ./client_app restore file.txt restored.txt | 최신 버전 복구 |
| ./client_app restore file.txt restored.txt 1 | 특정 버전 복구 |
| ./client_app monitor watched | 실시간 파일 감시 |
| ./client_app status | 자동 백업 상태 확인 |
| ./client_app resume | 자동 백업 중단 해제 |

## 발표 시연 시나리오

### 1. 기본 백업 및 버전 관리

```bash
echo "Original data" > sample.txt
./client_app backup sample.txt

echo "Updated data" > sample.txt
./client_app backup sample.txt

./client_app list sample.txt
./client_app restore sample.txt original.txt 1
cat original.txt
```

### 2. 네트워크 단절 및 Resume

```bash
dd if=/dev/urandom of=large.bin bs=1M count=5

CYBERVAULT_TEST_DISCONNECT=1 \
    ./client_app backup large.bin

./client_app restore large.bin restored.bin
cmp large.bin restored.bin && echo "Integrity PASS"
```

테스트 옵션은 전송 도중 TCP 연결을 강제로 종료한다.
클라이언트는 자동으로 재연결하고 서버에 저장된 위치부터
나머지 데이터를 전송한다.

### 3. 실시간 파일 감시

```bash
mkdir -p watched
./client_app monitor watched
```

별도 터미널에서 다음 명령어를 실행한다.

```bash
echo "Safe data" > watched/safe.txt
sleep 2
./client_app list safe.txt
```

### 4. 이상 변경 탐지 및 정상 버전 보호

```bash
touch watched/danger.locked
sleep 2
./client_app status

echo "Modified data" > watched/safe.txt
sleep 2

./client_app list safe.txt
./client_app restore safe.txt safe_restored.txt
cat safe_restored.txt
```

의심 확장자가 감지되면 자동 백업이 중단되며,
기존 정상 백업 버전은 유지된다.

### 5. 자동 백업 재개

```bash
./client_app resume
echo "New data" > watched/new.txt
sleep 2
./client_app list new.txt
```

## 테스트 결과

- AES-256-GCM 암호화 및 복호화: PASS
- SHA-256 무결성 검사: PASS
- 잘못된 암호화 키 차단: PASS
- 암호화 파일 변조 탐지: PASS
- 빈 파일 백업 및 복구: PASS
- 10MB 파일 백업 및 복구: PASS
- 동시 백업 및 버전 저장: PASS
- 원격 백업 및 버전별 복구: PASS
- 실시간 자동 백업: PASS
- 이상 변경 탐지 및 백업 중단: PASS
- 자동 백업 재개: PASS
- 서버 인증 및 비인가 접근 차단: PASS
- TCP 연결 단절 및 전송 이어받기: PASS
- 최종 통합 테스트: PASS

## 한계점 및 향후 개선 사항

- 이상 변경 탐지는 규칙 기반 휴리스틱으로 오탐 가능성이 있다.
- 서버 인증 토큰은 TLS 없이 전송되므로 통신 보안 강화가 필요하다.
- Resume은 실행 중 연결 단절을 대상으로 하며,
  클라이언트 종료 후 재실행을 통한 이어받기는 지원하지 않는다.
- 오래된 체크포인트의 자동 정리 기능이 필요하다.
- 동일한 이름의 파일은 서버에서 동일한 버전 그룹으로 관리된다.
- ACK 유실 상황과 실제 외부 네트워크 환경은 추가 검증이 필요하다.
- 하위 디렉터리의 재귀적 감시 기능은 향후 개선 사항이다.

