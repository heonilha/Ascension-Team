# AI 공동 작성 표기

2026-10-27 사용자 요청으로 Codex가 실질적으로 참여한 새 커밋에 아래 트레일러를 붙인다. 기존 작성자·커미터 설정과 이전 커밋 ID는 유지한다.

```text
문서: 공동 작성 표기 안내 추가

Co-authored-by: Codex <codex@openai.com>
```

## 추가와 중단

- 추가: 커밋 메시지 본문 마지막에 빈 줄과 위의 Co-authored-by 줄을 넣어 Push한다. GitHub가 이메일을 계정에 연결하면 커밋에 프로필이 표시된다.
- 앞으로 중단: 새 커밋에서 이 줄을 생략하고 AGENTS.md의 관련 규칙을 중단 요청에 맞게 수정한다. 모든 팀원 커밋에 자동으로 붙이는 Git 훅은 설치하지 않는다.
- 이미 Push한 커밋에서 제거: 메시지를 바꿔야 하므로 커밋 ID와 이후 이력이 달라질 수 있다. 공유 이력을 무조건 amend/rebase/force-push하지 않고 팀과 먼저 협의한다. 앞으로 생략하더라도 과거 표기는 남는다.

## 표시가 의미하는 것

공동 작성 표시는 AI의 작업 참여를 기록한다. 저장소 초대·Collaborator 권한·로그인 연결이 아니며, Codex 계정에 코드 접근 권한이나 관리 권한을 주지 않는다. 실제 게임 파일과 기본 작성자의 계정 설정도 바꾸지 않는다. 사진의 커밋 작성자 옆 프로필과 Contributors 집계는 서로 다른 표시이며, 후자의 반영 시점과 집계 여부는 GitHub가 결정한다.

## 확인한 근거

- OpenAI의 실제 공동 작성 커밋: https://github.com/openai/swarm/commit/6af0b4c
- OpenAI 저장소의 커밋 표기 예시: https://github.com/openai/symphony/blob/main/.codex/skills/commit/SKILL.md
- GitHub 공동 작성 트레일러 안내: https://docs.github.com/en/pull-requests/how-tos/commit-changes/creating-a-commit-with-multiple-authors
