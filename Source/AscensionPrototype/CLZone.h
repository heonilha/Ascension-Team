// 자기장 표시(판정은 서버). 지금 자기장 둘레에 반투명 파란 벽을 세우고, 다음 축소가 끝날 자리를 바닥에 흰 띠로 그린다.
// 둘 다 엔진 기본 상자를 원 둘레에 이어 붙인 판(Panels)이다. 재질은 CreateMap.py가 만든 M_ZoneWall·M_ZoneNext.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CLZone.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;

UCLASS()
class ACLZone : public AActor
{
    GENERATED_BODY()
public:
    ACLZone();
    virtual void BeginPlay() override;
    // Radius·NextRadius는 cm. NextRadius가 Radius보다 작을 때만 다음 자리 띠를 보인다. bShow=false면 모두 숨긴다.
    void Show(const FVector2D& Center, float Radius, float NextRadius, bool bShow);
    bool HasMaterials() const { return WallMaterial && NextMaterial; }
    void LogState() const; // 첫 판의 상태를 로그로(표시 확인용)

    static constexpr int32 Panels = 128;
    static constexpr float WallHeight = 2400.f; // 24m. 멀리서도 보이고 캐릭터 머리 위로 충분히 높다

private:
    void Place(TArray<TObjectPtr<UStaticMeshComponent>>& Ring, const FVector2D& Center, float Radius, float Z, float Width, float Height, bool bVisible);

    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Wall;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Next;
    UPROPERTY() TObjectPtr<UMaterialInterface> WallMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> NextMaterial;
    FVector2D LastCenter = FVector2D(1e9, 1e9);
    float LastRadius = -1, LastNext = -1;
    bool bLastShow = false;
};
