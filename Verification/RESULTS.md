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

2026-10-11 몬스터 모델 최종 검증: BuildMonsterFinal.log 빌드 성공. ImportUndead.log에서 별도 메시·스켈레톤·재질·대기·달리기·검 공격 저장 완료. MonsterView.log에서 ASCENSION_MONSTER_MODEL_LOADED clips=3/3 세 역할 모두 로드. Monsters.png 실제 화면 확인. MonsterSpawn.log: 20개 시드의 적 모두 UndeadSentinelRig이며 임시 몸체 숨김, 종류 조합·거리·배치·다음 전투 장비 유지 검사 PASS seeds=20. 최종 에셋 적용 후 Combat.log: ASCENSION_VERIFY PASS wall_trials=20. Meshy 잔액 API 1,642, 이번 26/누적 58크레딧. 도형 검·애니메이션 전환은 초기 수준이다.

2026-10-11 이동·체력 검증: BuildInputHealth.log 성공, DodgeVerify PASS cases=8(공격 중 보관한 입력으로 실제 회피 이동), FacingVerify PASS attacks=8(일반 dot 약0.995, 강공격 약0.78: 자연스러운 몸 비틀림 허용, 공격 방향은 고정). 기존 전투 회귀 PASS wall_trials=20. HealthLow.png에서 25/100·붉은 25% 게이지 확인. 이전 몸 정면 dot 1 강제 고정 기록은 이번 부드러운 보정으로 대체한다.

2026-10-11 생존 모드 검증: BuildSurvivalReady 성공. Scripts/VerifySurvival.ps1 / Survival.log에서 경험치 레벨업·세계 일시정지, 성장 선택 후 재개/장비 ID 유지, 번개+칼날 피해, 구슬 한 번 획득, 시간별 압박 증가, 일반 적 24/보스 포함 25 제한, 300초 경계의 보스 단일 생성, 보스 처치 후 탈출/장비 유지, 사망 성장 초기화, 새 생명 초기화 10개 항목 모두 PASS. GrowthChoice.png 렌더링 직접 확인. 최종 기능 적용 후 기존 Combat.log PASS wall_trials=20, SpawnSurvivalRegression.log PASS seeds=20. 300초 경계는 시각값 주입 검사이며 실제 5분 난이도 시험이 아니다. 보스는 기존 엘리트 외형·2연격을 재사용하며 보조 능력은 초기 효과다. Meshy 추가 비용 0.

2026-10-11 검기·추적 검증: BuildBladeFinal 빌드 성공. Survival.log PASS: 검기 2명 관통/각 1회 피해, 벽에서 소멸, 900cm 거리의 몬스터 추적 입력, 공격 재사용 대기 중 실제 이동 거리와 속도 확인. 경험치·자동 보조 공격·보스·탈출·사망 초기화의 기존 생존 검사도 PASS. Combat.log 기존 근접 회귀 PASS wall_trials=20. 실패했던 검기 시험 위치는 기둥을 가로지르고 있어 개방된 중앙으로 옮겼으며 벽 차단 시험은 별도로 유지했다. 검기 효과와 이동 중 상체 동작은 초기 수준이다.

BladeWave.png 실제 렌더링에서 이동 중 청록색 검기와 체력/경험치 UI 확인.

2026-10-11 추적 실패 수정: 기존 원거리 검사에는 이동 입력 확인만 있었음을 확인했다. 컨트롤러 없는 몬스터의 이동 물리 활성화 후 CombatFeel.log에서 실제 X축 접근 164.3cm/0.8초, 속도 220cm/s 확인. 피격 시 후퇴와 정지, 반복 타격의 경직 연장 방지, 회복 후 실제 추적 재개, 벽 앞 넉백 충돌, 엘리트/보스의 짧은 반응과 준비 공격 유지 모두 PASS. 검증 전용 스크린샷의 동결 몬스터는 물리 속도와 입력도 멈춘다. 기존 900cm 이동 입력 검사는 제거하고 실제 위치 검증으로 대체했다.

2026-10-11 검기·충전·상하체 동작 검증: 빌드 성공. VerifyCombat에서 이동 중 검 동작/실제 이동, 0.72초 재사용 제한, 지연 발사 1회, 충전 중 발사 없음, 1.2초 상한, 현재 카메라 방향, 최대 충전 70 피해/반경 110/관통 8, 회피·가방·레벨업·사망 취소와 초기화 확인. VerifyCombat -View는 MovingSlashEarly/MovingSlash/ChargingWave/ChargedRelease/WaveRoll 화면을 저장한다. 실제 렌더링에서 배우 이동을 제거한 상대 좌표 기준 발 약54.7cm·검 손 약113.8cm 변화, 몸 정면 dot0.940 확인. 충전 게이지와 주황 검기, 입력 방향의 전신 회피 PASS.

화면 시험의 첫 실행에서는 스크린샷 저장/셰이더 지연으로 두 비교 지점이 인접 프레임에 걸려 동작 변화 검사에 실패했다. 검사 기준 거리 3cm는 유지하고 최소 8개 평가 프레임 뒤의 포즈를 비교하도록 바꿨으며 재실행 PASS. 원본 동작을 재사용한 기본 보간이며 이동 방향별 전용 옆걸음/후진 클립은 아직 없다. 생존 성장·자동 공격·장비 유지·보스·탈출·사망 회귀 PASS. 기존 근접/벽 회피 검증도 유지한다. 난이도와 조작 느낌은 사용자 재플레이가 필요하다. Meshy 추가 사용 0.

2026-10-11 일반 검기 대상 수와 관통 강화: CombatFeel.log에서 먼 적을 먼저 생성해도 가까운 적만 피해 20을 받고 기본 검기가 소멸함을 확인했다. 실제 레벨업 3번 선택 후 검기 강화 1단계·대상 수 2·장비 ID 유지·세계 재개 확인. 강화 검기가 앞의 두 적에게 각각 피해 23을 주고 세 번째 적은 맞히지 않는 검사 PASS. 성장 사망 초기화와 새 생명의 기본 대상 수 1 검사도 PASS. 생존 관통 회귀의 시험용 검기는 관통 수 3을 명시해 기본 공격과 구분했다.

2026-10-11 긴 충전 범위 검증: 최종 빌드 성공. CombatFeel.log ASCENSION_COMBAT_VERIFY PASS. 3초 실제 충전 상한·최대 검기 피해105/반경230/관통10, 반충전 시간값을 설정한 발사 피해70/반경150 확인. 판정 폭 바깥 190cm 옆 적은 반충전에 맞지 않고 완충전에는 피해105를 받는 검사 PASS. 기존 실제 추적·피격 회복·이동 동작·충전 취소 및 생명 초기화 검사를 함께 통과했다. Survival.log ASCENSION_SURVIVAL_VERIFY PASS(성장·보조 공격·장비 ID·보스·탈출·사망/재시작 회귀). VerifyCombat -View ASCENSION_COMBAT_FEEL_VIEW PASS: 이동 중 손/발 변화와 정면, 충전 단계, 입력 방향의 전신 회피 확인. Meshy 추가 사용0.

시각 기록: ChargingWave.png(초기 충전), ChargingWaveMid.png(중간 충전), ChargingWaveFull.png(3초 최대·3배 피해·3.3배 폭), ChargedRelease.png(넓은 주황 검기·밝은 내부 띠) 직접 확인. MovingSlashEarly/MovingSlash와 WaveRoll도 최신 실행으로 갱신했다. 이미지와 원본 로그는 대응하며 로그 파일은 Git 제외 규칙을 유지한다.

2026-10-11 최소 충전 검증: BuildMinimumCharge.log 빌드 성공. CombatFeel.log ASCENSION_COMBAT_VERIFY PASS: 즉시 해제와 0.599초 해제 시 공격 시작/종료 시간이 바뀌지 않고 발사 대기가 생기지 않음, 취소 직후 재충전, 0.601초 해제 시 기존 0.85초 발사 동작 시작 및 검기 1회(피해 약49.02/반경102.05) 확인. 최대 3초와 반충전 피해/범위, 단일 대상/관통 성장, 실제 추적·넉백·취소/사망 초기화의 기존 검사도 통과. Survival.log ASCENSION_SURVIVAL_VERIFY PASS. CombatFeelView.log ASCENSION_COMBAT_FEEL_VIEW PASS, ChargingWave.png에서 발사 가능 문구와 최소 충전 기준선이 게이지 안에 표시되는 것을 직접 확인. 이번에는 별도 쿨타임을 추가하지 않았으며 탐욕 각성의 기존 추가 후딜레이는 유지한다. 최신 검증 화면 갱신. Meshy 추가 사용 0.

2026-10-11 드롭 숨김/칼날 외형 반전: BuildHiddenDrops.log 빌드 성공. VerifyDemo의 Combat.log에서 hidden_equipment_drop_cannot_be_looted PASS(사망 장비 가방이 숨김 상태이고 근처 직접 획득도 거절, 보관 장비 수 유지). 테스트용 가방만 명시적으로 다시 표시하여 기존 거리 제한·각 아이템 1회 이전·장비 교체/방어 검사를 유지했다. ASCENSION_VERIFY PASS wall_trials=20. CombatFeel.log ASCENSION_COMBAT_VERIFY PASS(단일 대상·관통 강화·최소 충전·반/완충전 피해와 범위·추적·넉백·취소). CombatFeelView.log ASCENSION_COMBAT_FEEL_VIEW PASS. ChargedRelease.png 직접 확인: 전방으로 볼록한 주황 칼날과 밝은 내부 띠, 처치 위치의 드롭 모델/라벨 없음, HUD의 E 획득 안내 제거. 검기의 이동·충돌 방향은 변경하지 않았다. 최신 7개 화면 갱신, Meshy 추가 사용 0.
