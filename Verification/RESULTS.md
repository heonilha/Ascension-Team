# 검증 기록

몸 방향 추가 검증: 스크린샷 전용 실행은 실제 키보드·마우스 입력을 차단한다. 양쪽 어깨와 머리·골반으로 모델의 실제 정면을 계산했다. 수정 전 카메라 Yaw 0도에서 모델 정면 -30도(dot 0.866), 수정 후 -0.4도(dot 1.000)로 확인했다. visible_model_faces_camera PASS. 대기·이동에 동작 자체의 방향을 보정하며 공격·구르기 회전은 해당 동작을 유지한다. BuildBodyForward.log 빌드 성공.

날짜: 2026-10-11 (한국 시간). Unreal Engine 5.8.3.

- BuildRandomCombat.log: Editor 모듈 빌드 성공(6.25초).
- Combat.log: 기존 전투·각성·장비·이동·회피 시험 통과. ASCENSION_VERIFY PASS wall_trials=20.
- RandomSpawn.log: 난수 시드 100~119 모두 7마리 생성, 종류 조합·시작 거리·서로 간 거리 확인. 시드 변경 시 배치 변화 확인. 전투 중 다음 차수 진입 거절, 전멸 후 다음 차수 8마리·장비 ID 유지 확인. ASCENSION_RANDOM_VERIFY PASS seeds=20.
- 효과음: 외부 에셋 없이 Scripts/CreateFeedbackAudio.ps1로 0.18초 PCM WAV 합성, Content/Audio/SwordImpact로 임포트 성공.
- 랜덤 전투·명중 실제 화면: RandomCombat.png(7마리), CombatImpact.png(20 피해·중앙 명중 표시·불꽃) 직접 확인. 초기 대기와 달리기의 서로 다른 전방 축을 분리해 보정했고 명중 디버그 구를 제거했다. BuildCombatFinal.log에서 최종 시각 수정 빌드 성공.

원본 엔진 로그는 로컬 Verification에 보존하고 Git에는 요약과 화면을 기록한다. 자동 시험은 재미·난이도와 청감 품질을 검증하지 않는다. 타격감·다수 적 전투는 사용자 재플레이가 필요하다. 전체 D1/D2 완료가 아니다.

2026-10-11 공격 정면 검사: -AscensionView -AscensionFacingVerify로 실제 렌더링 후 4방향 × 일반/강공격 8개 경우 검사. ASCENSION_FACING_VERIFY PASS attacks=8, 모든 실제 몸 정면 dot 1.000. AttackFacing.png, HeavyFacing.png 확인. BuildMonsterFacing.log 빌드 성공. 공격 중 이동 차단 등 기존 회귀 시험 ASCENSION_VERIFY PASS wall_trials=20. 기존 기록에서 공격 회전을 유지한다고 설명한 내용은 이번 보정으로 갱신한다.
