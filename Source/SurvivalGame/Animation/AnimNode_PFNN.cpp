// Copyright 2018 Sticks & Stones. All Rights Reserved.

#include "AnimNode_PFNN.h"
#include "PFNNAnimInstance.h"
#include "Gameplay/Movement/TrajectoryComponent.h"

#include "AnimInstanceProxy.h"
#include "MachineLearning/PhaseFunctionNeuralNetwork.h"
#include "PlatformFilemanager.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <ThirdParty/glm/gtx/transform.inl>

#include <fstream>

UPhaseFunctionNeuralNetwork* FAnimNode_PFNN::PFNN = nullptr;

FAnimNode_PFNN::FAnimNode_PFNN(): Trajectory(nullptr), PFNNAnimInstance(nullptr), FrameCounter(0), bIsPFNNLoaded(false)
{
}

void FAnimNode_PFNN::LoadData()
{
	LoadXForms();
	LoadPFNN();
}

void FAnimNode_PFNN::LoadXForms()
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	const FString RelativePath = FPaths::ProjectDir();
	const FString FullPathParents = RelativePath + FString::Printf(TEXT("Content/MachineLearning/PhaseFunctionNeuralNetwork/character_parents.bin"));
	const FString FullPathXforms = RelativePath + FString::Printf(TEXT("Content/MachineLearning/PhaseFunctionNeuralNetwork/character_xforms.bin"));

	IFileHandle* FileHandle = PlatformFile.OpenRead(*FullPathParents);
	if (FileHandle == nullptr)
	{
		//UE_LOG(NeuralNetworkLoading, Fatal, TEXT("Fatal error, Failed to load charater parents"));
		return;
	}
	float JointParentsFloat[JOINT_NUM];
	FileHandle->Read(reinterpret_cast<uint8*>(JointParentsFloat), sizeof(JointParentsFloat));

	for (int i = 0; i < JOINT_NUM; i++)
	{
		JointParents[i] = static_cast<int>(JointParentsFloat[i]);
	}


	FileHandle = PlatformFile.OpenRead(*FullPathXforms);
	if (FileHandle == nullptr)
	{
		//UE_LOG(NeuralNetworkLoading, Fatal, TEXT("Fatal error, Failed to load character xforms"));
		return;
	}
	FileHandle->Read(reinterpret_cast<uint8*>(JointRestXform), sizeof(JointRestXform));

	for (int i = 0; i < JOINT_NUM; i++)
	{
		JointRestXform[i] = glm::transpose(JointRestXform[i]);
	}

	delete FileHandle;
}

void FAnimNode_PFNN::LoadPFNN()
{
	if((PFNN == nullptr || !bIsPFNNLoaded) && Trajectory)
	{
		PFNN = NewObject<UPhaseFunctionNeuralNetwork>();
		bIsPFNNLoaded = PFNN->LoadNetworkData(Trajectory->GetOwner());
	}
}

void FAnimNode_PFNN::ApplyPFNN()
{

	Trajectory->TickTrajectory();

	auto RootPosition = Trajectory->GetRootPosition();
	auto RootRotation = Trajectory->GetRootRotation();

	//Input trajectiory positions and directions
	for (int i = 0; i < UTrajectoryComponent::LENGTH; i += 10)
	{
		int w = UTrajectoryComponent::LENGTH / 10;
		const glm::vec3 Position = glm::inverse(RootRotation) * (Trajectory->Positions[i] - RootPosition);
		const glm::vec3 Direction = glm::inverse(RootRotation) * Trajectory->Directions[i];
		PFNN->Xp((w * 0) + i / 10) = Position.x;
		PFNN->Xp((w * 1) + i / 10) = Position.z;
		PFNN->Xp((w * 2) + i / 10) = Direction.x;
		PFNN->Xp((w * 3) + i / 10) = Direction.z;
	}

	// Input trajectory gaits
	for (int i = 0; i < UTrajectoryComponent::LENGTH; i += 10)
	{
		const int w = UTrajectoryComponent::LENGTH / 10;
		PFNN->Xp((w * 4) + i / 10) = Trajectory->GaitStand[i];
		PFNN->Xp((w * 5) + i / 10) = Trajectory->GaitWalk[i];
		PFNN->Xp((w * 6) + i / 10) = Trajectory->GaitJog[i];
		PFNN->Xp((w * 7) + i / 10) = 0; //Unused input for crouch?;
		PFNN->Xp((w * 8) + i / 10) = Trajectory->GaitJump[i];
		PFNN->Xp((w * 9) + i / 10) = 0; //Unused input
	}

	//Input previous join position / velocity / rotations
	const glm::vec3 PreviousRootPosition = Trajectory->GetPreviousRootPosition();
	const glm::mat3 PreviousRootRotation = Trajectory->GetRootRotation();

	for (int i = 0; i < JOINT_NUM; i++)
	{
		const int o = ((UTrajectoryComponent::LENGTH / 10)*10);
		const glm::vec3 Position = glm::inverse(PreviousRootRotation) * (JointPosition[i] - PreviousRootPosition);
		const glm::vec3 Previous = glm::inverse(PreviousRootRotation) * JointVelocitys[i];
		//Magical numbers are indexes for the PFNN
		PFNN->Xp(o + (JOINT_NUM * 3 * 0) + i * 3 + 0) = Position.x;
		PFNN->Xp(o + (JOINT_NUM * 3 * 0) + i * 3 + 1) = Position.y;
		PFNN->Xp(o + (JOINT_NUM * 3 * 0) + i * 3 + 2) = Position.z;
		PFNN->Xp(o + (JOINT_NUM * 3 * 1) + i * 3 + 0) = Previous.x;
		PFNN->Xp(o + (JOINT_NUM * 3 * 1) + i * 3 + 1) = Previous.y;
		PFNN->Xp(o + (JOINT_NUM * 3 * 1) + i * 3 + 2) = Previous.z;
	}

	//Input heights for the trajectory
	for (int i = 0; i < UTrajectoryComponent::LENGTH; i += 10)
	{
		const int o = (((UTrajectoryComponent::LENGTH) / 10)*10) + JOINT_NUM * 3 * 2;
		const int w = UTrajectoryComponent::LENGTH / 10;

		const glm::vec3 PositionRight	= Trajectory->Positions[i] + (Trajectory->Rotations[i] * glm::vec3(Trajectory->Width, 0, 0));
		const glm::vec3 PositionLeft	= Trajectory->Positions[i] + (Trajectory->Rotations[i] * glm::vec3(-Trajectory->Width, 0, 0));

		FCollisionQueryParams TraceParams = FCollisionQueryParams(FName(TEXT("GroundGeometryTrace")), true, Trajectory->GetOwner());
		TraceParams.bTraceComplex = true;
		TraceParams.bTraceAsyncScene = true;
		TraceParams.bReturnPhysicalMaterial = false;

		const float DistanceLenght = 10000;
		FHitResult HitResultLeft(ForceInit);
		FHitResult HitResultRight(ForceInit);

		const FVector UPositionRight = FVector(PositionRight.x, PositionRight.y, PositionRight.z);
		const FVector UPositionLeft = FVector(PositionLeft.x, PositionLeft.y, PositionLeft.z);

		Trajectory->GetOwner()->GetWorld()->LineTraceSingleByChannel(HitResultRight, UPositionLeft, -FVector::UpVector * DistanceLenght, ECC_Pawn, TraceParams);
		Trajectory->GetOwner()->GetWorld()->LineTraceSingleByChannel(HitResultLeft, UPositionRight, -FVector::UpVector * DistanceLenght, ECC_Pawn, TraceParams);

		//TODO: Add height addition
		PFNN->Xp(o + (w * 0) + (i / 10)) = 0;/*HitResultRight.Location.Z*/ //0 - RootPosition.y;
		PFNN->Xp(o + (w * 1) + (i / 10)) = Trajectory->Positions[i].y; /*- RootPosition.z*/
		PFNN->Xp(o + (w * 2) + (i / 10)) = 0;/*HitResultLeft.Location.Z-*/ //0 - RootPosition.y;
	}
	
	Phase = 0;
	//Preform regression
	PFNN->Predict(Phase);

	//Build local transformation for the joints
	for (int i = 0; i < JOINT_NUM; i++)
	{
		const int OPosition = 8 + (((UTrajectoryComponent::LENGTH / 2) / 10) * 4) + JOINT_NUM * 3 * 0;
		const int OVelocity = 8 + (((UTrajectoryComponent::LENGTH / 2) / 10) * 4) + JOINT_NUM * 3 * 1;
		const int ORoation = 8 + (((UTrajectoryComponent::LENGTH / 2) / 10) * 4) + JOINT_NUM * 3 * 2;

		const glm::vec3 Position = (RootRotation * glm::vec3(PFNN->Yp(OPosition + i * 3 + 0), PFNN->Yp(OPosition + i * 3 + 1), PFNN->Yp(OPosition + i * 3 + 2))) + RootPosition;
		const glm::vec3 Velocity = (RootRotation * glm::vec3(PFNN->Yp(OVelocity + i * 3 + 0), PFNN->Yp(OVelocity + i * 3 + 1), PFNN->Yp(OVelocity + i * 3 + 2)));
		const glm::mat3 Rotation = (RootRotation * glm::toMat3(QuaternionExpression(glm::vec3(PFNN->Yp(ORoation + i * 3 + 0), PFNN->Yp(ORoation + i * 3 + 1), PFNN->Yp(ORoation + i * 3 + 2)))));

		JointPosition[i] = glm::mix(JointPosition[i] + Velocity, Position, Trajectory->ExtraJointSmooth);
		JointVelocitys[i] = Velocity;
		JointRotations[i] = Rotation;

		JointGlobalAnimXform[i] = glm::transpose(glm::mat4(
			Rotation[0][0], Rotation[1][0], Rotation[2][0], Position[0],
			Rotation[0][1], Rotation[1][1], Rotation[2][1], Position[1],
			Rotation[0][2], Rotation[1][2], Rotation[2][2], Position[2],
			0, 0, 0, 1));
	}

	for (int i = 0; i < JOINT_NUM; i++)
	{
		if (i == 0)
		{
			JointAnimXform[i] = JointGlobalAnimXform[i];
		}
		else
		{
			JointAnimXform[i] = glm::inverse(JointGlobalAnimXform[JointParents[i]]) * JointGlobalAnimXform[i];
		}
	}

	//Forward kinematics
	for (int i = 0; i < JOINT_NUM; i++)
	{
		JointGlobalAnimXform[i] = JointAnimXform[i];
		JointGlobalRestXform[i] = JointRestXform[i];
		int j = JointParents[i];
		while (j != -1)
		{
			JointGlobalAnimXform[i] = JointAnimXform[j] * JointGlobalAnimXform[i];
			JointGlobalRestXform[i] = JointRestXform[j] * JointGlobalRestXform[i];
			j = JointParents[j];
		}
		JointMeshXform[i] = JointGlobalAnimXform[i] * glm::inverse(JointGlobalRestXform[i]);
	}

	Trajectory->UpdatePastTrajectory();

	//Update current trajectory
	float StandAmount = powf(1.0f - Trajectory->GaitStand[UTrajectoryComponent::LENGTH / 2], 0.25f);

	const glm::vec3 TrajectoryUpdate = Trajectory->Rotations[UTrajectoryComponent::LENGTH / 2] * glm::vec3(PFNN->Yp(0), 0, PFNN->Yp(1)); //TODEBUG: Rot
	Trajectory->Positions[UTrajectoryComponent::LENGTH / 2] = Trajectory->Positions[UTrajectoryComponent::LENGTH / 2] + StandAmount * TrajectoryUpdate;
	Trajectory->Directions[UTrajectoryComponent::LENGTH / 2] = glm::mat3(glm::rotate(StandAmount * -PFNN->Yp(2), glm::vec3(0, 1, 0))) * Trajectory->Directions[UTrajectoryComponent::LENGTH / 2]; //TODEBUG: Rot
	Trajectory->Rotations[UTrajectoryComponent::LENGTH / 2] = glm::mat3(glm::rotate(atan2f(
		Trajectory->Directions[UTrajectoryComponent::LENGTH / 2].x,
		Trajectory->Directions[UTrajectoryComponent::LENGTH / 2].z), glm::vec3(0, 1, 0)));

	//TODO: Add wall logic

	//Update future trajectory
	for (int i = UTrajectoryComponent::LENGTH / 2 + 1; i < UTrajectoryComponent::LENGTH; i++)
	{
		const int W = (UTrajectoryComponent::LENGTH / 2) / 10;
		const float M = fmod((static_cast<float>(i) - (UTrajectoryComponent::LENGTH / 2)) / 10.0, 1.0);
		//Trajectory->Positions[i].x = (glm::normalize(TrajectoryTargetDirectionNew).x);
		//Trajectory->Positions[i].y = (glm::normalize(TrajectoryTargetDirectionNew).y);
		//Trajectory->Directions[i].x = TrajectoryTargetDirectionNew.x;
		//Trajectory->Directions[i].y = TrajectoryTargetDirectionNew.y;

		Trajectory->Positions[i].x = (1 - M) * PFNN->Yp(8 + (W * 0) + (i / 10) - W) + M * PFNN->Yp(8 + (W * 0) + (i / 10) - W + 1); 
		Trajectory->Positions[i].z = (1 - M) * PFNN->Yp(8 + (W * 1) + (i / 10) - W) + M * PFNN->Yp(8 + (W * 1) + (i / 10) - W + 1); 
		Trajectory->Directions[i].x = (1 - M) * PFNN->Yp(8 + (W * 2) + (i / 10) - W) + M * PFNN->Yp(8 + (W * 2) + (i / 10) - W + 1);
		Trajectory->Directions[i].z = (1 - M) * PFNN->Yp(8 + (W * 3) + (i / 10) - W) + M * PFNN->Yp(8 + (W * 3) + (i / 10) - W + 1);

		Trajectory->Positions[i] = (Trajectory->Rotations[UTrajectoryComponent::LENGTH / 2] * Trajectory->Positions[i]) + Trajectory->Positions[UTrajectoryComponent::LENGTH / 2];
		Trajectory->Directions[i] = glm::normalize((Trajectory->Rotations[UTrajectoryComponent::LENGTH / 2] * Trajectory->Directions[i]));
		Trajectory->Rotations[i] = glm::mat3(glm::rotate(atan2f(Trajectory->Directions[i].x, Trajectory->Directions[i].z), glm::vec3(0, 1, 0)));
	}


	FinalBoneLocations.Empty();
	FinalBoneRotations.Empty();

	FinalBoneLocations.SetNum(JOINT_NUM);
	FinalBoneRotations.SetNum(JOINT_NUM);
	const float FinalScale = 1.0f;

	for (int32 i = 0; i < JOINT_NUM; i++)
	{
		FinalBoneLocations[i] = FVector(JointPosition[i].x, JointPosition[i].y, JointPosition[i].z);

		FMatrix UMatrix;
		UMatrix.M[0][0] = JointMeshXform[i][0][0];	UMatrix.M[1][0] = JointMeshXform[i][1][0];	UMatrix.M[2][0] = JointMeshXform[i][2][0];	UMatrix.M[3][0] = JointMeshXform[i][3][0];
		UMatrix.M[0][1] = JointMeshXform[i][0][1];	UMatrix.M[1][1] = JointMeshXform[i][1][1];	UMatrix.M[2][1] = JointMeshXform[i][2][1];	UMatrix.M[3][1] = JointMeshXform[i][3][1];
		UMatrix.M[0][2] = JointMeshXform[i][0][2];	UMatrix.M[1][2] = JointMeshXform[i][1][2];	UMatrix.M[2][2] = JointMeshXform[i][2][2];	UMatrix.M[3][2] = JointMeshXform[i][3][2];
		UMatrix.M[0][3] = 0;						UMatrix.M[1][3] = 0;						UMatrix.M[2][3] = 0;						UMatrix.M[3][3] = 1;

		const glm::quat Rotation = JointRotations[i];
		FinalBoneRotations[i] = FQuat(Rotation.x, Rotation.z, Rotation.y, Rotation.w);
	}
	
	//Phase update
//	Phase = fmod(Phase + (StandAmount * 0.9f + 0.1f) * 2.0f * PI * PFNN->Yp(3), 2.0f * PI);

	FrameCounter++;
	if (FrameCounter == 1)
	{
		Trajectory->LogTrajectoryData(FrameCounter);
		LogNetworkData(FrameCounter);
	}


}

glm::quat FAnimNode_PFNN::QuaternionExpression(const glm::vec3 arg_Length)
{
	float W = glm::length(arg_Length);

	const glm::quat Quat = W < 0.01 ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f) : glm::quat(
		cosf(W),
		arg_Length.x * (sinf(W) / W),
		arg_Length.y * (sinf(W) / W),
		arg_Length.z * (sinf(W) / W));

	return Quat / sqrtf(Quat.w*Quat.w + Quat.x*Quat.x + Quat.y*Quat.y + Quat.z*Quat.z);
}

UPFNNAnimInstance * FAnimNode_PFNN::GetPFNNInstanceFromContext(const FAnimationInitializeContext& arg_Context)
{
	FAnimInstanceProxy* AnimProxy = arg_Context.AnimInstanceProxy;
	if (AnimProxy)
	{
		return Cast<UPFNNAnimInstance>(AnimProxy->GetAnimInstanceObject());
	}
	return nullptr;
}

UPFNNAnimInstance * FAnimNode_PFNN::GetPFNNInstanceFromContext(const FAnimationUpdateContext & arg_Context)
{
	FAnimInstanceProxy* AnimProxy = arg_Context.AnimInstanceProxy;
	if (AnimProxy)
	{
		return Cast<UPFNNAnimInstance>(AnimProxy->GetAnimInstanceObject());
	}
	return nullptr;
}

void FAnimNode_PFNN::Initialize_AnyThread(const FAnimationInitializeContext& arg_Context)
{
	FAnimNode_Base::Initialize_AnyThread(arg_Context);
	
	EvaluateGraphExposedInputs.Execute(arg_Context);

	PFNNAnimInstance = GetPFNNInstanceFromContext(arg_Context);
	if (!PFNNAnimInstance) 
	{
		UE_LOG(LogTemp, Error, TEXT("PFNN Animation node should only be added to a PFNNAnimInstance child class!"));
	}
}

void FAnimNode_PFNN::Update_AnyThread(const FAnimationUpdateContext& arg_Context)
{
	FAnimNode_Base::Update_AnyThread(arg_Context);

	if (!bIsPFNNLoaded) 
	{
		LoadData();
	}

	EvaluateGraphExposedInputs.Execute(arg_Context);

	if (PFNNAnimInstance) 
	{
		Trajectory = PFNNAnimInstance->GetOwningTrajectoryComponent();
	}
	
	if(Trajectory != nullptr && bIsPFNNLoaded)
		ApplyPFNN();
}

void FAnimNode_PFNN::Evaluate_AnyThread(FPoseContext& arg_Output)
{
	const FTransform& CharacterTransform = arg_Output.AnimInstanceProxy->GetActorTransform();
	if (FinalBoneLocations.Num() >= JOINT_NUM && FinalBoneRotations.Num() >= JOINT_NUM) 
	{
		auto Bones = arg_Output.Pose.GetBoneContainer();
		auto Skeleton = Bones.GetReferenceSkeleton();

		for (int32 i = 0; i < JOINT_NUM; i++)
		{
			const FCompactPoseBoneIndex RootBoneIndex(i);

			const FCompactPoseBoneIndex ParentBoneIndex(Bones.GetParentBoneIndex(RootBoneIndex));

			arg_Output.Pose[RootBoneIndex].SetLocation(FinalBoneLocations[i]);		
			arg_Output.Pose[RootBoneIndex].SetRotation(FinalBoneRotations[i]);
			
			arg_Output.AnimInstanceProxy->AnimDrawDebugSphere(arg_Output.Pose[RootBoneIndex].GetLocation(), 2.5f, 12, FColor::Green, false, -1.0f);
			
			if (ParentBoneIndex != -1) 
			{
				arg_Output.AnimInstanceProxy->AnimDrawDebugLine(arg_Output.Pose[RootBoneIndex].GetLocation(), arg_Output.Pose[ParentBoneIndex].GetLocation(), FColor::Blue,false,-1,5.0f);
			}

		}
		arg_Output.Pose.NormalizeRotations();
	}
	else 
	{
		UE_LOG(PFNN_Logging, Error, TEXT("PFNN results were not properly applied!"));
	}
}

void FAnimNode_PFNN::LogNetworkData(int arg_FrameCounter) 
{
	try 
	{
		std::fstream fs;
		fs.open("UE4_Network.log", std::ios::out);

		if (fs.is_open()) 
		{
			fs << "UE4_Network" << std::endl;
			fs << "Network Frame[" << arg_FrameCounter << "]" << std::endl << std::endl;
			
			fs << "Current Phase: " << Phase << std::endl << std::endl;

			fs << "Joints" << std::endl;
			for (size_t i = 0; i < JOINT_NUM; i++)
			{
				fs << "Joint[" << i << "]" << std::endl;
				fs << "	JointPosition: " << JointPosition[i].x << "X, " << JointPosition[i].y << "Y, " << JointPosition[i].z << "Z" << std::endl;
				fs << "	JointVelocitys: " << JointVelocitys[i].x << "X, " << JointVelocitys[i].y << "Y, " << JointVelocitys[i].z << "Z" << std::endl;

				for (size_t x = 0; x < 3; x++)
				{
					fs << "	JointRotations:  " << JointRotations[i][x].x << "X, " << JointRotations[i][x].y << ", " << JointRotations[i][x].z << std::endl;
				}

				for (size_t x = 0; x < 3; x++)
				{
					fs << "	JointAnimXform:  " << JointAnimXform[i][x].x << "X, " << JointAnimXform[i][x].y << ", " << JointAnimXform[i][x].z << std::endl;
				}

				for (size_t x = 0; x < 3; x++)
				{
					fs << "	JointRestXform:  " << JointRestXform[i][x].x << "X, " << JointRestXform[i][x].y << ", " << JointRestXform[i][x].z << std::endl;
				}

				for (size_t x = 0; x < 3; x++)
				{
					fs << "	JointMeshXform:  " << JointMeshXform[i][x].x << "X, " << JointMeshXform[i][x].y << ", " << JointMeshXform[i][x].z << std::endl;
				}

				for (size_t x = 0; x < 3; x++)
				{
					fs << "	JointGlobalRestXform:  " << JointGlobalRestXform[i][x].x << "X, " << JointGlobalRestXform[i][x].y << ", " << JointGlobalRestXform[i][x].z << std::endl;
				}

				for (size_t x = 0; x < 3; x++)
				{
					fs << "	JointGlobalAnimXform:  " << JointGlobalAnimXform[i][x].x << "X, " << JointGlobalAnimXform[i][x].y << ", " << JointGlobalAnimXform[i][x].z << std::endl;
				}

				fs << "JoinParents: " << JointParents[i] << std::endl;
			}
			fs << "End Joints" << std::endl << std::endl;

			fs << "FinalLocations" << std::endl;
			for (size_t i = 0; i < FinalBoneLocations.Num(); i++)
			{
				fs << "Bone[" << i << "]" << std::endl;
				fs << "	FinalBoneLocation: " << FinalBoneLocations[i].X << "X, " << FinalBoneLocations[i].Y << "Y, " << FinalBoneLocations[i].Z << "Z" << std::endl;
				fs << "	FinalBoneRotation: " << FinalBoneRotations[i].X << "X, " << FinalBoneRotations[i].Y << "Y, " << FinalBoneRotations[i].Z << "Z, " << FinalBoneRotations[i].W << "W"<< std::endl;
			}
			fs << "End FinalLocations" << std::endl;

		}
		else 
		{
			throw std::exception();
		}
	}
	catch (std::exception e) 
	{
#ifdef WITH_EDITOR
		UE_LOG(LogTemp, Log, TEXT("Failed to log network data"));
#endif
	}
	
}