// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "ExtrudePolygon.h"
#include "IMeshEditorModeEditingContract.h"
#include "IMeshEditorModeUIContract.h"
#include "UICommandInfo.h"
#include "EditableMesh.h"
#include "MeshElement.h"
#include "MultiBoxBuilder.h"
#include "UICommandList.h"
#include "ViewportInteractor.h"

#define LOCTEXT_NAMESPACE "MeshEditorMode"


void UExtrudePolygonCommand::FindExtrudeDistanceUsingInteractor(
	IMeshEditorModeEditingContract& MeshEditorMode,
	UViewportInteractor* ViewportInteractor,
	const UEditableMesh* EditableMesh,
	const FVector AxisOrigin,
	const FVector AxisDirection,
	const float AxisLength,
	float& OutExtrudeDistance )
{
	check( ViewportInteractor != nullptr );

	OutExtrudeDistance = 0.0f;

	float ClosestDistanceToRay = MAX_flt;
	float BestExtrudeDistance = 0.0f;


	// Find the interactor ray that is closest to the axis line, and determine the distance from the origin of the
	// axis to that point
	// @todo mesheditor grabber: Needs grabber sphere support
	FVector LaserStart, LaserEnd;
	const bool bLaserIsValid = ViewportInteractor->GetLaserPointer( /* Out */ LaserStart, /* Out */ LaserEnd );
	if( bLaserIsValid )
	{
		const FVector AxisSegmentStart = AxisOrigin - AxisDirection * ( AxisLength * 0.5f );
		const FVector AxisSegmentEnd = AxisOrigin + AxisDirection * ( AxisLength * 0.5f );

		FVector ClosestPointOnAxis, ClosestPointOnRay;
		FMath::SegmentDistToSegmentSafe(
			AxisSegmentStart, AxisSegmentEnd,
			LaserStart, LaserEnd,
			/* Out */ ClosestPointOnAxis,
			/* Out */ ClosestPointOnRay );
		const float DistanceToRay = ( ClosestPointOnAxis - ClosestPointOnRay ).Size();

		if( DistanceToRay < ClosestDistanceToRay )
		{
			ClosestDistanceToRay = DistanceToRay;

			const FVector AxisToClosestPointVector = ( ClosestPointOnAxis - AxisOrigin );
			BestExtrudeDistance = AxisToClosestPointVector.Size();

			// Check to see if the closest point is actually behind the origin of the axis (negative extrusion distance)
			const FVector ClosestPointDirection = AxisToClosestPointVector.GetSafeNormal();
			if( FVector::DotProduct( ClosestPointDirection, AxisDirection ) < 0.0f )
			{
				BestExtrudeDistance *= -1.0f;
			}
		}
	}

	OutExtrudeDistance = BestExtrudeDistance;
};



void UExtrudePolygonCommand::RegisterUICommand( FBindingContext* BindingContext )
{
	UI_COMMAND_EXT( BindingContext, /* Out */ UICommandInfo, "ExtrudePolygon", "Extrude Polygon Mode", "Set the primary action to extrude polygons.", EUserInterfaceActionType::RadioButton, FInputChord() );
}


bool UExtrudePolygonCommand::TryStartingToDrag( IMeshEditorModeEditingContract& MeshEditorMode, UViewportInteractor* ViewportInteractor )
{
	// @todo mesheditor: Need a "Shift+Click+Drag" extrude instead of having to explicitly equip the action
	bool bHaveExtrudeAxis = false;
	{
		const FMeshElement& PolygonElement = MeshEditorMode.GetHoveredMeshElement( ViewportInteractor );
		if( PolygonElement.ElementAddress.ElementType == EEditableMeshElementType::Polygon )
		{
			// Is it selected?
			if( MeshEditorMode.IsMeshElementSelected( PolygonElement ) )
			{
				const UEditableMesh* EditableMesh = MeshEditorMode.FindEditableMesh( *PolygonElement.Component, PolygonElement.ElementAddress.SubMeshAddress );
				if( EditableMesh != nullptr )
				{
					this->ExtrudePolygonAxisOrigin = ViewportInteractor->GetHoverLocation();

					// Use the polygon normal as the extrude axis direction
					{
						UPrimitiveComponent* Component = PolygonElement.Component.Get();
						check( Component != nullptr );
						const FMatrix ComponentToWorldMatrix = Component->GetRenderMatrix();

						const FPolygonRef PolygonRef( PolygonElement.ElementAddress.SectionID, FPolygonID( PolygonElement.ElementAddress.ElementID ) );
						const FVector ComponentSpacePolygonNormal = EditableMesh->ComputePolygonNormal( PolygonRef );

						const FVector WorldSpacePolygonNormal = ComponentToWorldMatrix.TransformVector( ComponentSpacePolygonNormal ).GetSafeNormal();

						this->ExtrudePolygonAxisDirection = WorldSpacePolygonNormal;
					}

					bHaveExtrudeAxis = true;
				}
			}
		}
	}

	return bHaveExtrudeAxis;
}


void UExtrudePolygonCommand::ApplyDuringDrag( IMeshEditorModeEditingContract& MeshEditorMode, UViewportInteractor* ViewportInteractor, bool& bOutShouldDeselectAllFirst, TArray<FMeshElement>& OutMeshElementsToSelect )
{
	static TMap< UEditableMesh*, TArray< FMeshElement > > MeshesWithPolygonsToExtrude;
	MeshEditorMode.GetSelectedMeshesAndPolygons( /* Out */ MeshesWithPolygonsToExtrude );

	if( MeshesWithPolygonsToExtrude.Num() > 0 )
	{
		// Deselect the mesh elements before we delete them.  This will make sure they become selected again after undo.
		MeshEditorMode.DeselectMeshElements( MeshesWithPolygonsToExtrude );

		for( auto& MeshAndPolygons : MeshesWithPolygonsToExtrude )
		{
			UEditableMesh* EditableMesh = MeshAndPolygons.Key;
			const TArray<FMeshElement>& PolygonsToExtrude = MeshAndPolygons.Value;

			static TArray<FPolygonRef> PolygonRefsToExtrude;
			PolygonRefsToExtrude.Reset();
			for( const FMeshElement& PolygonToExtrude : PolygonsToExtrude )
			{
				FPolygonRef PolygonRef( PolygonToExtrude.ElementAddress.SectionID, FPolygonID( PolygonToExtrude.ElementAddress.ElementID ) );
				PolygonRefsToExtrude.Add( PolygonRef );
			}


			// Figure out how far the extruded polygon should be from where it started
			float ExtrudeDistance = 0.0f;
			{
				const float AxisLength = 10000.0f;	// @todo mesheditor tweak (ideally should be infinite)
				FindExtrudeDistanceUsingInteractor(
					MeshEditorMode,
					ViewportInteractor,
					EditableMesh,
					ExtrudePolygonAxisOrigin,
					ExtrudePolygonAxisDirection,
					AxisLength,
					/* Out */ ExtrudeDistance );
			}


			{
				verify( !EditableMesh->AnyChangesToUndo() );

				// Position the new polygon to where the interactor is
				UPrimitiveComponent* Component = PolygonsToExtrude[ 0 ].Component.Get();	// NOTE: All polygons in this array belong to the same mesh/component, so we just need the first element
				check( Component != nullptr );
				// @todo mesheditor: We're working with a float here, so we'll treat the component scale as a scalar (X)
				const float ComponentSpaceExtrudeDistance = ExtrudeDistance / Component->ComponentToWorld.GetScale3D().X;

				// Create a copy of the polygon with new extruded polygons for each edge
				static TArray<FPolygonRef> NewExtrudedFrontPolygons;
				NewExtrudedFrontPolygons.Reset();
				EditableMesh->ExtrudePolygons( PolygonRefsToExtrude, ComponentSpaceExtrudeDistance, /* Out */ NewExtrudedFrontPolygons );


				// Make sure the new polygons are selected.  The old polygon was deleted and will become deselected automatically.
				for( int32 NewPolygonNumber = 0; NewPolygonNumber < NewExtrudedFrontPolygons.Num(); ++NewPolygonNumber )
				{
					const FPolygonRef& NewExtrudedFrontPolygon = NewExtrudedFrontPolygons[ NewPolygonNumber ];

					const FMeshElement& MeshElement = PolygonsToExtrude[ NewPolygonNumber ];

					FMeshElement NewExtrudedPolygonMeshElement;
					{
						NewExtrudedPolygonMeshElement.Component = MeshElement.Component;
						NewExtrudedPolygonMeshElement.ElementAddress = MeshElement.ElementAddress;
						NewExtrudedPolygonMeshElement.ElementAddress.SectionID = NewExtrudedFrontPolygon.SectionID;
						NewExtrudedPolygonMeshElement.ElementAddress.ElementID = NewExtrudedFrontPolygon.PolygonID;
					}

					// Queue selection of this new element.  We don't want it to be part of the current action.
					OutMeshElementsToSelect.Add( NewExtrudedPolygonMeshElement );
				}
			}

			MeshEditorMode.TrackUndo( EditableMesh, EditableMesh->MakeUndo() );
		}
	}
}


void UExtrudePolygonCommand::AddToVRRadialMenuActionsMenu( IMeshEditorModeUIContract& MeshEditorMode, FMenuBuilder& MenuBuilder, TSharedPtr<FUICommandList> CommandList, const FName TEMPHACK_StyleSetName, class UVREditorMode* VRMode )
{
	if( MeshEditorMode.GetMeshElementSelectionMode() == EEditableMeshElementType::Polygon )
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT( "VRExtrudePolygon", "Extrude" ),
			FText(),
			FSlateIcon( TEMPHACK_StyleSetName, "MeshEditorMode.PolyExtrude" ),
			MakeUIAction( MeshEditorMode ),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
}



#undef LOCTEXT_NAMESPACE

