# Tools 사용법

## NewScript.ps1 — MonoBehaviour 스크립트 생성


```bash
Canape.slnx 있는 위치에서 실행
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/NewScript.ps1 (스크립트 이름) -Folder (폴더 이름)
```

### 예시
```bash
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/NewScript.ps1 EnemyAI -Folder Enemy
```

### "-Folder(폴더 이름)" 입력 안할 시 Script 폴더에 생성


