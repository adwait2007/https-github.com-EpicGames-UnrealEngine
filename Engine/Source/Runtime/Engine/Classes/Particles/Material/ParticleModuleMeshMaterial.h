// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.


#pragma once
#include "Particles/Material/ParticleModuleMaterialBase.h"
#include "ParticleModuleMeshMaterial.generated.h"

UCLASS(editinlinenew, hidecategories=Object, MinimalAPI, meta=(DisplayName = "Mesh Material"))
class UParticleModuleMeshMaterial : public UParticleModuleMaterialBase
{
	GENERATED_UCLASS_BODY()

	/** The array of materials to apply to the mesh particles. */
	UPROPERTY(EditAnywhere, Category=MeshMaterials)
	TArray<class UMaterialInterface*> MeshMaterials;


	//Begin UParticleModule Interface
	virtual void	Spawn(FParticleEmitterInstance* Owner, int32 Offset, float SpawnTime, FBaseParticle* ParticleBase) OVERRIDE;
	virtual uint32	RequiredBytesPerInstance(FParticleEmitterInstance* Owner = NULL) OVERRIDE;

#if WITH_EDITOR
	virtual bool	IsValidForLODLevel(UParticleLODLevel* LODLevel, FString& OutErrorString) OVERRIDE;
#endif
	//End UParticleModule Interface
};



