# 검증 기록

날짜: 2026-10-11 (한국 시간). Unreal Engine 5.8.3.

- BuildRandomCombat.log: Editor 모듈 빌드 성공(6.25초).
- Combat.log: 기존 전투·각성·장비·이동·회피 시험 통과. ASCENSION_VERIFY PASS wall_trials=20.
- RandomSpawn.log: 난수 시드 100~119 모두 7마리 생성, 종류 조합·시작 거리·서로 간 거리 확인. 시드 변경 시 배치 변화 확인. 전투 중 다음 차수 진입 거절, 전멸 후 다음 차수 8마리·장비 ID 유지 확인. ASCENSION_RANDOM_VERIFY PASS seeds=20.
- 효과음: 외부 에셋 없이 Scripts/CreateFeedbackAudio.ps1로 0.18초 PCM WAV 합성, Content/Audio/SwordImpact로 임포트 성공.
- 랜덤 전투·명중 실제 화면: RandomCombat.png(7마리), CombatImpact.png(20 피해·중앙 명중 표시·불꽃) 직접 확인. 초기 대기와 달리기의 서로 다른 전방 축을 분리해 보정했고 명중 디버그 구를 제거했다. BuildCombatFinal.log에서 최종 시각 수정 빌드 성공.

원본 엔진 로그는 로컬 Verification에 보존하고 Git에는 요약과 화면을 기록한다. 자동 시험은 재미·난이도와 청감 품질을 검증하지 않는다. 타격감·다수 적 전투는 사용자 재플레이가 필요하다. 전체 D1/D2 완료가 아니다.
