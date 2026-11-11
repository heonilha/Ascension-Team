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

2026-10-11 전투장/지속 증원 검증: BuildHordeFinal.log 최종 빌드 성공. ExpandMap.log ASCENSION_MAP_READY width=3600 depth=3200 existing=True, 생성한 맵을 원본 Content/Maps에 반영했다. Survival.log ASCENSION_SURVIVAL_VERIFY PASS: 네 모서리(±1670,±1470)의 바닥와6마리 안전 생성, 실제 생존 시작12마리, 정기 증원 사이의4마리 보충, 초기4마리/2.5초 및후반8마리/1.5초, 48마리 채움/상한, 보스단일생성 및49마리, 기존 성장·보조공격·탈출·사망 초기화 확인. CombatFeel.log ASCENSION_COMBAT_VERIFY PASS(실제추적·넉백·충전·검기, 확장된 동쪽 벽 충돌). Combat.log ASCENSION_VERIFY PASS wall_trials=20(새 동쪽 벽의 회피 차단 및 장비/드롭숨김 회귀). Scripts/VerifyHorde.ps1의 HordeView.log ASCENSION_HORDE_VIEW PASS: 48마리 렌더링, 추적 측정시36마리 실제속도>10cm/s. ExpandedArena.png 및 HordeCombat.png 직접 확인. 화면은 생성/추적 후 적을 정지한 검증 장면이며 장시간 FPS/5분 난이도 시험을 뜻하지 않는다. Meshy 추가 사용0.

2026-10-11 개방형 폐허 검증: BuildOpenRuinsFinal.log 빌드 성공. OpenRuinsMapFinal.log ASCENSION_OPEN_RUINS_READY playable_width=5600 playable_depth=4800 props=177. 기존 외벽과 이전 Open_ 장식을 교체하고 하늘/조명/시작점을 유지해 두 번째 실행에도 개방형 맵이 정상 저장됐다. 맵 및8개 재질을 원본 Content에 반영했다. Survival.log ASCENSION_SURVIVAL_VERIFY PASS: 새모서리(±2670,±2270) 바닥와6마리 안전증원, 시작12·정기/보충 생성·48상한·보스49·성장·탈출·사망/재시작 확인. CombatFeel.log ASCENSION_COMBAT_VERIFY PASS: 새로운 동쪽 경계 넉백/검기, 추적·충전·관통 등 기존 전투 검사(화면 가독성 정리 전 실행이며 이후 변경은 박스/라벨 표시 조건뿐이다). 최종 Combat.log ASCENSION_VERIFY PASS wall_trials=20, 새 동쪽 경계의 회피 차단과 장비/숨김 규칙 확인. 최종 HordeView.log ASCENSION_HORDE_VIEW PASS moving=40 count=48. ExpandedArena.png에서 폐허·돌길·외곽 바위·높은 벽 제거 확인, HordeCombat.png에서 정상 시점과 하늘색 배경·개발용 박스/일반 적 전체 라벨 제거 확인. 초기 도형 지형이며 장시간 FPS나 다음 층 이동 검증을 뜻하지 않는다. Meshy 추가 사용0.

2026-10-11 중앙 수문장/층진행 검증: BuildFloorGateFinal.log 성공. 수정된 CentralGateMap.log ASCENSION_PAVING_FLAT PASS, ASCENSION_OPEN_RUINS_READY props=178. Survival.log 최종 ASCENSION_SURVIVAL_VERIFY PASS:300초경과후자동보스/층이동없음, 1층모든적체력고정, 처치전출구봉인, 중앙밖소환거부, 중앙수문장1회/기존48+보스49/추가증원중단, 체력300/중앙위치, 보상선택중층이동차단, 실제랜덤14마리2층/성장·장비ID유지/보스상태재설정,2층체력25%증가고정 및수문장450, 휴식3층의적0·증원차단·피해차단·체력100·성장/장비유지, 사망/새생명1층초기화와기존성장·관통·벽검기회귀 PASS. CombatFeel.log ASCENSION_COMBAT_VERIFY PASS:실제추적·경직·넉백·최소/완충전·성장 초기화 등 기존검사. Scripts/VerifyFloors.ps1/FloorView.log 최종 ASCENSION_FLOOR_VIEW PASS:보스소환·2층14마리·휴식적0검사 및4장면저장. GatekeeperFight.png에서중앙수문장300체력바/출구봉인, FloorExitOpen.png초록출구, SecondFloor.png2층/14마리, RestArea.png휴식도착/100체력 직접확인. 맵회전수정뒤생존/화면재검증PASS. 화면용처치는10000데미지값주입이며 난이도/멀티합류/몹몰이방지 검증은아니다. 실제진행은1인용전역모드이며 지형을재사용한다. Meshy 추가사용0.

2026-10-26 수문장 패턴 검증: Verification/BuildBossFinal.log Result: Succeeded. Scripts/VerifyBoss.ps1 / BossCombat.log ASCENSION_BOSS_VERIFY PASS(17항목). 준비 중 피해 없음/피격 시 예고시간 유지, 방향고정 후 옆걸음 회피, 베기1회피해20/1.6초빈틈, 후방 안전, 내려찍기400cm피해28/430cm안전, 회피무적, 돌진방향고정/900cm상한/회복, 큰프레임에서 플레이어 스윕 접촉1회피해24, 동쪽경계충돌/즉시회복, 체력50%격노의 이후예고/회복단축, 근거리패턴순환, 900cm원거리돌진선택, 처치후출구개방 PASS. 추가로 실제 월드 프레임에서 예고 중 피해 없이 기다리고, 플레이어가 옆으로 이동한 후에도 고정 방향으로650cm 이상/910cm 이하 이동하여 회복 상태로 전환하는 검사 PASS. 경계/피해/격노 일부 검사는 위치·체력·시각값을 주입한 기능검사이며 장시간 난이도 시험은 아니다.

최종 소스로 Scripts/VerifyCombat.ps1의 CombatFeel.log ASCENSION_COMBAT_VERIFY PASS(단일대상/관통성장/충전/실제추적/넉백/취소/사망, 보스 피격 시 예고 유지), Scripts/VerifySurvival.ps1의 Survival.log ASCENSION_SURVIVAL_VERIFY PASS(중앙소환/보스단일생성/2층/휴식/성장·장비유지/고정체력) 확인. Scripts/VerifyBoss.ps1 -View / BossView.log ASCENSION_BOSS_VIEW PASS. BossCleave.png, BossCharge.png, BossSlamEnraged.png의 부채꼴/직선/원형 바닥 예고와 하단 체력바·한국어 회피 안내·격노 표시를 직접 확인했다. 화면용 위치와 패턴·체력은 검증 목적으로 설정하며, 피격 무적을 사용해 세 장면을 저장한다. 실제 조작 난이도/장시간 성능 검증을 뜻하지 않는다. 전용 모션/최종 이펙트/효과음은 후속 작업이다. Meshy 추가 사용0, 추가 결제0.

2026-10-27 팀 저장소 분리 검증: 새 비공개 kimminseoung-source/Ascension-Team의 최초 main은91fc34c37cd5fc9f817ecb035f9f44f15ccb5fd5로 기존 로컬 HEAD와 일치한다. 기존39개 커밋의 이력을 재작성하지 않고 Push했다. 원래 저장소는 보존하고 로컬 origin은 팀 저장소로 전환했다. 게임 코드/에셋 수정이 없는 저장소·문서 작업이므로 빌드/전투 시험은 재실행하지 않았다. 팀원 PC에서 실행한 검증을 의미하지 않는다.

2026-10-27 공동 작성 표기: OpenAI 공개 저장소의 실제 Codex 공동 작성 커밋과 표기 예시, GitHub 트레일러 설명을 확인했다. 문서 및 작업 규칙만 변경했으며 git diff --check로 형식을 검사한다. 게임 코드·에셋 변경이 없어 빌드·전투 검사는 재실행하지 않는다. 커밋 작성 후 트레일러 파싱과 원격 업로드를 확인하며 과거 이력을 재작성하지 않는다.

2026-10-29 온라인 P0 Windows 접속 검증(feature/network-p0, 46e24a2): UE 5.8.3 Build.bat으로 AscensionPrototypeEditor Win64 Development 빌드 성공(152초, 컴파일 에러 0, CombatLab.cpp double→float 경고 C4305 5개는 기존 코드). 서버(Ascension-Server 5423c34)는 WSL Ubuntu 24.04·GCC 14.2·Ninja로 debug 프리셋 빌드 성공, 경고 0. 서버 원본과 클라이언트 AscensionProtocol.h 사본 동일(kVersion 1). 서버를 포트 7777·스레드 4로 실행하고 UnrealEditor-Cmd -game -nullrhi로 ue1(팀 1)을 127.0.0.1:7777에 접속한 뒤 봇 1개(--bots 1 --team-base 1 --seconds 10)를 같은 방에 붙였다.

- 클라이언트 로그: ASCENSION_NET_CONNECT, ASCENSION_NET_ENTER team=1 id=2 others=0 확인, ASCENSION_NET_FAILED 0회. 봇 입장 시점에 원격 플레이어 모델 로드(ASCENSION_MODEL_LOADED clips=5/5)가 한 번 더 찍혀 원격 캐릭터 생성을 확인했다.
- 서버 로그: "방 1 입장" 2회(ue1 슬롯 0, bot-0 슬롯 1, 인원 2/3), "이동 거부" 0회. 세션 1은 포트 확인용 연결로 로그인 전에 종료됐다.
- 판정: 요청 기준(ENTER 있음·FAILED 없음·입장 2회·이동 거부 없음) PASS.
- 봇 자체 결과는 FAIL(`다른 봇 이동 1/0`)이다. 봇은 같은 방의 다른 봇 수만 기대값으로 계산하므로 언리얼 클라이언트의 이동을 받으면 불일치로 판정한다. 봇이 ue1의 이동을 받았다는 뜻이며 보정 0회·RTT 1ms였다. 사람 클라이언트와 섞어 시험할 때 봇 기준을 조정할지는 서버 쪽에서 정해야 한다.
- 한계: 화면 없는 클라이언트 1개와 봇 1개 시험이다. 렌더링 화면의 HUD·원격 캐릭터 보간 모습, 실제 클라이언트 2개 접속, 다른 PC 접속은 확인하지 않았다. 원본 로그는 Git에 넣지 않았다.

2026-11-03 P1 서버 회피 판정·모드별 지연 보정 검증(클라이언트 feature/p1-dodge-lagcomp, 서버 feature/p1-dodge-lagcomp, 프로토콜 v2). UE 5.8.3 AscensionPrototypeEditor Win64 Development 빌드 성공(새 경고 없음, 기존 C4305 5개). 서버·봇은 WSL GCC 14.2 debug·tsan 빌드 모두 경고 0.

- 봇 시나리오 17개 PASS(서버 --pve-slam-ms 1500): 기존 팀 배정·정원 초과·순간이동 거부·회피 몰림 4개, PvE 회피 시험 지연 0/40/100/150/300ms, PvE 시각 조작·왕복 부풀리기, PvP 회피 시험 지연 0/50/100/150ms, PvP 시각 조작·왕복 부풀리기. 회피 판정 비교 111건(PvE 60, PvP 51) 불일치 0, 판정 경계 25ms 안 5건은 비교 제외. PvP 공격 봇의 공격은 모두 판정을 받음. 이동 거부는 순간이동 시험의 의도된 1건뿐, 회피 재사용 거부 0.
- 상한 확인: PvE 지연 300ms에서 명중 44ms 전에 누른 회피는 되감기 상한 200ms에 걸려 피격(지연 150ms 이하에서는 같은 입력이 회피). 시각 조작 봇은 왕복을 2000ms로 부풀려도 상한 안(PvE 61ms·PvP 61ms 지각)만 회피로 인정되고 상한 밖(PvE 261ms·PvP 162ms 지각)은 피격.
- PvP: 지연 150ms에서는 공격 준비 350ms 중 방어자가 보는 예고가 약 200ms로 줄어 계획보다 늦게 누르게 되는 것을 확인(판정은 기대값과 일치).
- ThreadSanitizer: 스레드 4개 서버에 봇 60개 + PvE 회피(지연 100ms) + PvP 회피·조작(지연 50ms) + 정원 초과 + 순간이동을 동시에 붙여 경고 0, 판정 295건. WSL 커널 주소 무작위화 때문에 setarch -R로 실행해야 TSan이 시작된다.
- 실제 UE 클라이언트(화면 없음, -AscensionNetAutoDodge): PvE에서 서버 내려찍기 공격 1·3·5 회피, 2·4·6 피격으로 의도와 일치, 서버 HP와 클라이언트 HP 일치. PvP에서 추격하는 공격 봇의 공격 1·3·5·7 회피(명중 100~149ms 전, 무적 300ms 안), 2·4·6 피격, 이동 거부 0.
- 실제 클라이언트 시험에서 찾아 고친 문제: (1) 서버 시계 추정이 게임 스레드 처리 시각을 써서 회피 시각이 114~229ms 이르게 찍혀 모두 피격 → 수신 스레드 도착 시각으로 측정. (2) 최소 왕복 측정 하나를 계속 써서 시계 속도 차이가 쌓임 → 0.2초 간격 Ping, 최근 1초 중 최소 왕복 사용. (3) 시험 봇의 정원 초과 판정이 봇 번호를 기준으로 해서 동시 접속 순서에 따라 실패 → 결과 개수로 판정.
- 환경 문제: 이 PC의 WSL 시계가 Windows보다 간헐적으로 빠르다(재시작 전 10%, 재시작 직후 0.06%에서 4.6%로 튐). clocksource 변경으로는 해결되지 않았다. 보강 후에도 클라이언트 시계 오차 10~40ms가 남아 회피가 계획보다 늦게 인정되지만 허용 여유(왕복/2 + 30ms) 안이다. 운영 서버 환경에서 다시 재야 한다.
- 한계: 렌더링 화면에서 사람이 직접 조작한 회피 체감은 확인하지 않았다. 몬스터·수문장·검기·스킬은 아직 서버 판정이 아니며 PvE 서버 공격은 시험 패턴뿐이다. 봇이 사람 클라이언트와 같은 방에 있으면 "다른 봇 이동" 개수 불일치로 FAIL을 출력하는 기존 문제는 그대로다(회피 판정 비교는 PASS). 원본 로그는 Git에 넣지 않았다.

2026-11-03 P1 서버 충돌 지형 검증(클라이언트·서버 feature/p1-server-terrain).

- 내보내기: Scripts/ExportServerCollision.py가 CombatLab 맵의 충돌 액터 45개(막힘 37, 오를 수 있는 낮은 구조물 8)를 Data/ServerCollision/CombatLab.txt로 내보냄. 맵은 저장하지 않음.
- 실제 충돌과 비교: -AscensionCollisionVerify(게임 실행, 40cm 격자, 주인공 캡슐 반지름 32·반높이 88)에서 ASCENSION_COLLISION_VERIFY PASS points=16678 blocked=953 mismatched=0 edge_skipped=122. 첫 시도는 엄폐 바위 주변 84곳 불일치였고, 원인은 늘린 구 충돌이 가장 작은 축 반지름(95cm) 구로 처리되는 엔진 동작이었다. 내보내기를 같은 기준으로 고친 뒤 일치. 에디터 커맨드렛 월드는 물리 상태가 없어 충돌 질의가 모두 비어서 검증을 게임 실행으로 옮겼다.
- 서버 단위 시험 ascension_terrain_test 21개 PASS: 기둥 중심·반지름 경계, 상인방 아래 통과, 기둥 받침 위 서기, 25도 회전 벽, 엄폐 바위 반지름 95, 경계벽, 높이 범위, 기둥 관통 이동 거부·옆 통과·기둥 둘레 정상 이동 통과, 시야 차단·개방·상인방 아래, PvE 3·PvP 16 시작 위치.
- 봇 시나리오 18개 PASS(기존 17개 + 기둥 돌파 --wall). 기둥 돌파 봇은 지형 거부 15회로 되돌려졌고, 다른 시나리오의 정상 이동 지형 오판 0. 회피 판정 비교 112건 불일치 0(경계 제외 5). 이동 거부는 순간이동 1건과 기둥 돌파 15건뿐.
- ThreadSanitizer: 지형을 공유하는 서버에 봇 60개 + 기둥 돌파 + PvE·PvP 회피 시험 동시, 경고 0.
- 한계: 공격 엄폐 판정은 단위 시험으로만 확인했고 봇·실제 클라이언트로 기둥 뒤 회피 장면은 시험하지 않았다. 이동은 여전히 클라이언트 위치 전송 + 서버 검증이다(서버 이동 계산은 다음 단계). 경사·계단 지형은 없어서 2D + 높이 범위로 충분하다. 맵을 바꾸면 다시 내보내 서버 data에 복사해야 한다.

2026-11-05 P1 서버 권한 이동 검증(클라이언트·서버 feature/p1-server-movement, 프로토콜 v3).

- 공유 이동 코드 protocol/AscensionMovement.h(클라이언트 복사본 동일): 60Hz 고정 틱, 달리기 450cm/s·가속 2048, 회피 39틱 300cm, 재사용 58틱, 지형 충돌(원 밀어내기·벽 따라 미끄러짐).
- 빌드: 서버·봇·지형 시험 GCC 14.2 debug·tsan 경고 0. UE 5.8.3 Editor 빌드 성공(새 경고 없음). 첫 UE 빌드에서 네임스페이스 별칭 Move가 언리얼 전역 Move와 충돌하고 지역 변수 Error가 멤버를 가려 실패했고, 이름을 바꿔 해결.
- 봇 시나리오 20개 + 지형 단위 시험 PASS. 정상 이동 예측 오차 최대 0.00cm(지연 100·300ms 포함). 속도 조작(입력 3배): 서버 이동 481cm/s(처음 0.5초 여유분 포함), 넘친 입력 버림, 예측 오차 60cm로 재조정됨. 기둥으로 계속 걷기: 장애물까지 최소 32.00cm(반지름 그대로, 파고듦 없음). 매 틱 회피: 5초에 서버 회피 6회(허용 6.2회 이하). 회피 판정 비교 112건 불일치 0(경계 제외 5).
- ThreadSanitizer: 봇 60개 + 속도 조작 + 기둥 + PvP 회피 + 회피 연타(지연 100ms) 동시, 경고 0.
- 실제 UE 클라이언트(화면 없음, -AscensionNetAutoMove, MSVC)와 GCC 서버: 원 달리기 + 2.5초마다 회피로 약 22초 동안 서버 확정 432회, 예측 오차 0.00cm, 보정 0회, 버린 입력 0. 같은 방 봇도 예측 오차 0.00cm.
- 1인 데모 회귀: ASCENSION_VERIFY PASS wall_trials=20, ASCENSION_SURVIVAL_VERIFY PASS(-AscensionVerify -AscensionSurvivalVerify 함께 실행. 처음에 생존 옵션만 주어 진행이 멈췄던 것은 실행 옵션 실수).
- 한계: 화면에서 사람이 조작한 손맛·카메라 흔들림은 확인하지 않았다. 온라인 이동은 2D라 기둥 받침에 오르지 않고, 회피가 벽에 닿으면 미끄러진다(1인 데모는 멈춤). 공격 중 이동 잠금은 클라이언트 입력 0으로 처리한다. R·층 이동 순간이동은 온라인에서 적용되지 않는다. 봇이 사람 클라이언트와 같은 방이면 "다른 봇 이동" 개수 불일치 FAIL이 나오는 기존 문제는 그대로다.

2026-11-11 서버 Windows 전환 검증(서버 feature/windows-msvc, 클라이언트 feature/p1-server-monsters).

- 서버를 WSL+GCC에서 Windows+MSVC(Visual Studio 2026, MSVC 19.51, C++23)로 옮겼다. asio는 유지(Windows에서는 IOCP). /W4 경고 0, Linux GCC 14.2 빌드도 경고 0.
- 서버 Scripts/Test.ps1: 지형 단위 시험 + 봇 시나리오 20개 병렬 실행, 21개 모두 PASS(약 30초). 회피 판정 비교 110건 불일치 0(경계 제외 5), 정상 이동 예측 오차 0.00cm, 속도 조작 봇 서버 속도 488cm/s(허용 상한 508).
- 실제 UE 클라이언트(화면 없음) + Windows 서버: 자동 회피가 계획한 명중 75ms 전에 74~76ms로 판정(공격 1·3·5·7 회피, 2·4·6·8 피격), 요청 되감기 1~2ms. 자동 이동 약 22초 동안 예측 오차 0.00cm·보정 0회·버린 입력 0.
- 찾아 고친 문제: (1) 화면 없는 실행은 2000fps를 넘는데 언리얼이 프레임 시간을 최소 0.5ms로 올려 잡아 게임 시간이 실제보다 약 16% 빨랐다. 이동 틱이 게임 시간을 써서 입력이 초당 약 70개 나갔고 서버가 초과분을 버려 예측 보정 15cm가 났다. 시험용 자동 회피 타이머도 일찍 눌려 전부 피격이었다. 이동 틱과 타이머를 실제 시간으로 바꿨다. 이전 WSL 시험에서는 WSL 시계가 빨라서 가려져 있었다. (2) 같은 PC에서 왕복 지연이 0ms로 재져 "미측정"과 구분되지 않던 것을 측정 후 1 이상으로 보고. (3) 서버 입력 처리권에 3% 회복 여유(긴 끊김 뒤 처리권이 바닥에 머무는 것 방지, 속도 조작 상한은 103%).
- WSL 시계가 Windows보다 빠르게 흐르던 문제(시계 오차 10~40ms)는 서버를 Windows에서 실행하면서 사라졌다.
- 한계: ThreadSanitizer는 MSVC에 없어 Linux 빌드에서만 돌린다(이번 전환 뒤에는 아직 다시 돌리지 않았다). 렌더링 화면의 사람 조작은 확인하지 않았다.
