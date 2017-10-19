#include "pch.h"

#include <Kore/IO/FileReader.h>
#include <Kore/Graphics4/Graphics.h>
#include <Kore/Graphics4/PipelineState.h>
#include <Kore/Graphics1/Color.h>
#include <Kore/Input/Keyboard.h>
#include <Kore/Input/Mouse.h>
#include <Kore/System.h>
#include <Kore/Log.h>

#include "MeshObject.h"
#include "RotationUtility.h"
#include "Logger.h"

#ifdef KORE_STEAMVR
#include <Kore/Vr/VrInterface.h>
#include <Kore/Vr/SensorState.h>
#endif

using namespace Kore;
using namespace Kore::Graphics4;

namespace {
	
#ifdef KORE_STEAMVR
	const int width = 2048;
	const int height = 1024;
#else
	const int width = 1024;
	const int height = 768;
#endif
	
	Logger* logger;
	bool logData = false;
	bool readData = false;
	int line = 0;
	const char* positionDataFilename = "positionData_1504264185.csv";
	const char* initialTransFilename = "initTransAndRot_1504264185.csv";

	double startTime;
	double lastTime;
	float fiveSec;
	
	VertexStructure structure;
	Shader* vertexShader;
	Shader* fragmentShader;
	PipelineState* pipeline;
	
	// Uniform locations
	TextureUnit tex;
	ConstantLocation pLocation;
	ConstantLocation vLocation;
	ConstantLocation mLocation;
	
	bool left, right = false;
	bool down, up = false;
	bool forward, backward = false;
	bool rotateX = false;
	bool rotateY = false;
	bool rotateZ = false;
	int mousePressX, mousePressY = 0;
	
	MeshObject* cube1;
	MeshObject* cube2;
	MeshObject* cube3;
	MeshObject* cube4;
	MeshObject* avatar;
	
#ifdef KORE_STEAMVR
	Quaternion cameraRotation = Quaternion(0, 0, 0, 1);
	vec3 cameraPosition = vec3(0, 0, 0);
	
	int leftTrackerIndex = -1;
	int rightTrackerIndex = -1;
	int leftFootTrackerIndex = -1;
	int rightFootTrackerIndex = -1;
#else
	Quaternion cameraRotation = Quaternion(0, 0, 0, 1);
	vec3 cameraPosition = vec3(0, 0.8, 1.8);
#endif
	
	float angle = 0;
	vec3 desPosition1 = vec3(0, 0, 0);
	vec3 desPosition2 = vec3(0, 0, 0);
	vec3 desPositionLeftFoot = vec3(0, 0, 0);
	vec3 desPositionRightFoot = vec3(0, 0, 0);
	Quaternion desRotation1 = Quaternion(0, 0, 0, 1);
	Quaternion desRotation2 = Quaternion(0, 0, 0, 1);
	Quaternion desRotationLeftFoot = Quaternion(0, 0, 0, 1);
	Quaternion desRotationRightFoot = Quaternion(0, 0, 0, 1);
	
	Quaternion initDesRotationLeftHand = Quaternion(0, 0, 0, 1);
	Quaternion initDesRotationRightHand = Quaternion(0, 0, 0, 1);
	
	mat4 initTransInv = mat4::Identity();
	mat4 initTrans = mat4::Identity();
	const mat4 hmdOffset = mat4::Translation(0, 0.2f, 0);
	Quaternion initRot = Quaternion(0, 0, 0, 1);
	Quaternion initRotInv = Quaternion(0, 0, 0, 1);
	
	bool initCharacter = false;

	// Tracker type for tracking Hands (Controller / ViveTracker)
	bool controllerForHands = true;
	
	// Left foot 49, right foot 53, Left hand 10, right hand 29
	const int leftHandBoneIndex = 10;
	const int rightHandBoneIndex = 29;
	const int leftFootBoneIndex = 49;
	const int rightFootBoneIndex = 53;
	const int renderTrackerOrTargetPosition = 0;		// 0 - dont render, 1 - render desired position, 2 - render target position

	void renderCube(MeshObject* cube, float x, float y, float z, Quaternion rotation) {
		cube->M = mat4::Translation(x, y, z) * rotation.matrix().Transpose();
		Graphics4::setMatrix(mLocation, cube->M);
		cube->render(tex);
	}

	void renderTracker(int mode) {
		switch (mode) {
			case 0:
				// Dont render
				break;
			case 1:
			{
				// Render desired position
				renderCube(cube1, desPosition1.x(), desPosition1.y(), desPosition1.z(), desRotation1);
				renderCube(cube2, desPosition2.x(), desPosition2.y(), desPosition2.z(), desRotation2);
				renderCube(cube3, desPositionLeftFoot.x(), desPositionLeftFoot.y(), desPositionLeftFoot.z(), desRotationLeftFoot);
				renderCube(cube4, desPositionRightFoot.x(), desPositionRightFoot.y(), desPositionRightFoot.z(), desRotationRightFoot);
				break;
			}
			case 2:
			{
				// Render target position
				vec3 targetPosition = avatar->getBonePosition(leftHandBoneIndex);
				Quaternion targetRotation = avatar->getBoneGlobalRotation(leftHandBoneIndex);

				renderCube(cube1, targetPosition.x(), targetPosition.y(), targetPosition.z(), targetRotation);

				targetPosition = avatar->getBonePosition(rightHandBoneIndex);
				targetRotation = avatar->getBoneGlobalRotation(rightHandBoneIndex);
				renderCube(cube2, targetPosition.x(), targetPosition.y(), targetPosition.z(), targetRotation);

				targetPosition = avatar->getBonePosition(leftFootBoneIndex);
				targetRotation = avatar->getBoneGlobalRotation(leftFootBoneIndex);
				renderCube(cube3, targetPosition.x(), targetPosition.y(), targetPosition.z(), targetRotation);

				targetPosition = avatar->getBonePosition(rightFootBoneIndex);
				targetRotation = avatar->getBoneGlobalRotation(rightFootBoneIndex);
				renderCube(cube4, targetPosition.x(), targetPosition.y(), targetPosition.z(), targetRotation);
				break;
			}
			default:
				break;
		}
	}
	
	Kore::mat4 getProjectionMatrix() {
		mat4 P = mat4::Perspective(45, (float)width / (float)height, 0.01f, 1000);
		P.Set(0, 0, -P.get(0, 0));
		return P;
	}
	
	Kore::mat4 getViewMatrix() {
		vec3 lookAt = cameraPosition + vec3(0, 0, -1);
		mat4 V = mat4::lookAt(cameraPosition, lookAt, vec3(0, 1, 0));
		V *= cameraRotation.matrix();
		return V;
	}
	
	void setDesiredPosition(Kore::vec3 desPosition, int boneIndex) {
		// Transform desired position to the character coordinate system
		vec4 finalPos = initTransInv * vec4(desPosition.x(), desPosition.y(), desPosition.z(), 1);
		avatar->setDesiredPosition(boneIndex, finalPos);
	}

	// desPosition and desRotation are global
	void setDesiredPositionAndOrientation(Kore::vec3 &desPosition, Kore::Quaternion &desRotation, const int boneIndex) {
		
		if (logData) {
			logger->saveData(desPosition, desRotation);
		}

		float offsetX = 0.0f;
		float offsetY = 0.0f;
		float offsetZ = 0.0f;
		float rotOffsetX = 0.0f;
		float rotOffsetY = 0.0f;
		float rotOffsetZ = 0.0f;

		// Hand offsets (ViveTracker)
		float handOffsetX = 0.02f;
		float handOffsetY = 0.02f;
		float handRotOffsetX = 0.0f;
		float handRotOffsetY = Kore::pi / 6.0f;
		float handRotOffsetZ = 0.0f;
		// Hand offsets (Controller)
		if (controllerForHands) {
			handOffsetX = 0.02f;
			handOffsetY = 0.0f;
			handRotOffsetX = Kore::pi;
			handRotOffsetY = 0.0f;
			handRotOffsetZ = Kore::pi / 4.0f;
		}
		// Foot offsets
		float footOffsetX = 0.08f;
		float footOffsetY = -0.06f;
		float footOffsetZ = 0.0f;
		float footRotOffsetX = -Kore::pi / 2.1f;

		Kore::Quaternion desRot = desRotation;

		// Set offset depending on given bone
		if (boneIndex == rightHandBoneIndex) {
			desRot.rotate(initDesRotationRightHand);
			offsetX = -handOffsetX;
			rotOffsetX = handRotOffsetX;
			rotOffsetY = -handRotOffsetY;
			rotOffsetZ = -handRotOffsetZ;
		} else if (boneIndex == leftHandBoneIndex) {
			desRot.rotate(initDesRotationLeftHand);
			offsetX = handOffsetX;
			rotOffsetX = handRotOffsetX;
			rotOffsetY = handRotOffsetY;
			rotOffsetZ = handRotOffsetZ;
		} else if (boneIndex == leftFootBoneIndex
			|| boneIndex == rightFootBoneIndex) {
			// Setting offset for controller tied to lower leg, above ankle, 
			// showing to the front, with green led showing down
			rotOffsetX = footRotOffsetX;
			offsetX = footOffsetX;
			offsetY = footOffsetY;
			offsetZ = footOffsetZ;
		}

		desRot.rotate(Kore::Quaternion(Kore::vec3(1, 0, 0), rotOffsetX));
		desRot.rotate(Kore::Quaternion(Kore::vec3(0, 1, 0), rotOffsetY));
		desRot.rotate(Kore::Quaternion(Kore::vec3(0, 0, 1), rotOffsetZ));
		
		desRotation = desRot;
		
		// Transform desired position to the bone
		Kore::mat4 curPos = 
			mat4::Translation(desPosition.x(), desPosition.y(), desPosition.z())
			* desRot.matrix().Transpose() 
			* mat4::Translation(offsetX, offsetY, offsetZ);
		Kore::vec4 desPos = curPos * vec4(0, 0, 0, 1);
		desPosition = vec3(desPos.x(), desPos.y(), desPos.z());
		
		// Transform desired position to the character local coordinate system
		vec4 finalPos = initTransInv * vec4(desPos.x(), desPos.y(), desPos.z(), 1);
		Kore::Quaternion finalRot = initRotInv.rotated(desRotation);
		
//		log(Info, "loc pos %f %f %f loc rot %f %f %f %f", finalPos.x(), finalPos.y(), finalPos.z(), finalRot.x, finalRot.y, finalRot.z, finalRot.w);
		
		avatar->setDesiredPositionAndOrientation(boneIndex, finalPos, finalRot);
	}
	
	void update() {
		float t = (float)(System::time() - startTime);
		double deltaT = t - lastTime;
		lastTime = t;
		
		fiveSec += deltaT;
		if (fiveSec > 1) {
			fiveSec = 0;
			
			float averageIt = avatar->getAverageIKiterationNum();
			
			if (logData) logger->saveLogData("it", averageIt);
			
			//log(Info, "Average iteration %f", averageIt);
		}
		
		const float speed = 0.01f;
		if (left) {
			cameraPosition.x() -= speed;
		}
		if (right) {
			cameraPosition.x() += speed;
		}
		if (forward) {
			cameraPosition.z() += speed;
		}
		if (backward) {
			cameraPosition.z() -= speed;
		}
		if (up) {
			cameraPosition.y() += speed;
		}
		if (down) {
			cameraPosition.y() -= speed;
		}
		
		Graphics4::begin();
		Graphics4::clear(Graphics4::ClearColorFlag | Graphics4::ClearDepthFlag, Graphics1::Color::Black, 1.0f, 0);
		
		Graphics4::setPipeline(pipeline);
		
#ifdef KORE_STEAMVR
		
		bool firstPersonVR = true;
		bool firstPersonMonitor = false;
		
		VrInterface::begin();
		SensorState state;
		
		if (!initCharacter) {
			// Get height of avatar to scale it to the y-pos of the hmd
			float currentAvatarHeight = avatar->getHeight();
			
			// Get Position of hmd
			state = VrInterface::getSensorState(0);
			vec3 hmdPos = state.pose.vrPose.position; // z -> face, y -> up down
			float currentUserHeight = hmdPos.y();
			
			// Set camera position depending on user-height
			cameraPosition.y() = currentUserHeight * 0.5;
			cameraPosition.z() = currentUserHeight * 0.5;
			
			// Scale the avatar
			float scale = currentUserHeight / currentAvatarHeight;
			avatar->setScale(scale);
			
			// Set initial transformation
			initTrans = mat4::Translation(hmdPos.x(), 0, hmdPos.z());

			// Initial transformation of both hands: z-axis (blue) -> direction of thumbs
			initDesRotationLeftHand.rotate(Quaternion(vec3(0, 1, 0), -Kore::pi / 2));
			initDesRotationRightHand.rotate(Quaternion(vec3(0, 1, 0), Kore::pi / 2));
			
			// Set initial orientation
			Quaternion hmdOrient = state.pose.vrPose.orientation;
			float zAngle = 2 * Kore::acos(hmdOrient.y);
			initRot.rotate(Quaternion(vec3(0, 0, 1), -zAngle));
			
			initRotInv = initRot.invert();
			
			// Set matrix of avatar, combined of initial transform and rotation matrices
			avatar->M = initTrans * initRot.matrix().Transpose() * hmdOffset;

			// Get inverse of the Trans-Matrix of the avatar, for positioning of the tracker
			initTransInv = (initTrans * initRot.matrix().Transpose() * hmdOffset).Invert();
			
			log(Info, "current avatar height %f, currend user height %f, scale %f", currentAvatarHeight, currentUserHeight, scale);
			
			// Get tracker indices of left/right hand and foot tracker
			VrPoseState controller;
			for (int i = 0; i < 16; ++i) {
				controller = VrInterface::getController(i);

				if (controller.trackedDevice == TrackedDevice::Controller
					|| controller.trackedDevice == TrackedDevice::ViveTracker) {

					vec3 trackerPos = controller.vrPose.position;
					vec4 trackerTransPos = initTransInv * vec4(trackerPos.x(), trackerPos.y(), trackerPos.z(), 1);
					
					log(Info, "trackerPos.y: %f", trackerPos.y());

					if (trackerPos.y() > currentUserHeight / 4) {
						// Hand tracker
						if (trackerTransPos.x() > 0) {
							log(Info, "leftTrackerIndex: %i -> %i", leftTrackerIndex, i);
							leftTrackerIndex = i;
						}
						else {
							log(Info, "rightTrackerIndex: %i -> %i", rightTrackerIndex, i);
							rightTrackerIndex = i;
						}
						//leftTrackerIndex = -1;
						//rightTrackerIndex = -1;
					}
					else {
						// Foot tracker
						if (trackerTransPos.x() > 0) {
							log(Info, "leftFootTrackerIndex: %i -> %i", leftFootTrackerIndex, i);
							leftFootTrackerIndex = i;
						}
						else {
							log(Info, "rightFootTrackerIndex: %i -> %i", rightFootTrackerIndex, i);
							rightFootTrackerIndex = i;
						}
						//leftFootTrackerIndex = i;
						//rightFootTrackerIndex = -1;
					}
				}
			}
			
			if (logData) {
				vec4 initPos = initTrans * vec4(0, 0, 0, 1);
				logger->saveInitTransAndRot(vec3(initPos.x(), initPos.y(), initPos.z()), initRot);
			}
			
			initCharacter = true;
		}
		
		// Update avatar-bones depending on controller positions
		VrPoseState controller;

		if (leftTrackerIndex != -1) {
			controller = VrInterface::getController(leftTrackerIndex);

			// Get controller position
			desPosition1 = controller.vrPose.position;
			// Get cont1roller rotation
			desRotation1 = controller.vrPose.orientation;

			//log(Info, "pos: %f %f %f, orient: %f %f %f %f", desPosition1.x(), desPosition1.y(), desPosition1.z(), desRotation1.w, desRotation1.x, desRotation1.y, desRotation1.z);
			
			setDesiredPositionAndOrientation(desPosition1, desRotation1, leftHandBoneIndex);
		}

		if (rightTrackerIndex != -1) {
			controller = VrInterface::getController(rightTrackerIndex);

			desPosition2 = controller.vrPose.position;
			desRotation2 = controller.vrPose.orientation;
				
			setDesiredPositionAndOrientation(desPosition2, desRotation2, rightHandBoneIndex);
		}

		if (leftFootTrackerIndex != -1) {
			controller = VrInterface::getController(leftFootTrackerIndex);

			desPositionLeftFoot = controller.vrPose.position;
			desRotationLeftFoot = controller.vrPose.orientation;

			setDesiredPositionAndOrientation(desPositionLeftFoot, desRotationLeftFoot, leftFootBoneIndex);
		}

		if (rightFootTrackerIndex != -1) {
			controller = VrInterface::getController(rightFootTrackerIndex);

			desPositionRightFoot = controller.vrPose.position;
			desRotationRightFoot = controller.vrPose.orientation;

			setDesiredPositionAndOrientation(desPositionRightFoot, desRotationRightFoot, rightFootBoneIndex);
		}
		
		// Render for each eye once
		for (int eye = 0; eye < 2; ++eye) {
			VrInterface::beginRender(eye);
			
			Graphics4::clear(Graphics4::ClearColorFlag | Graphics4::ClearDepthFlag, Graphics1::Color::Black, 1.0f, 0);
			
			state = VrInterface::getSensorState(eye);
			Graphics4::setMatrix(vLocation, state.pose.vrPose.eye);
			Graphics4::setMatrix(pLocation, state.pose.vrPose.projection);
			
			Graphics4::setMatrix(mLocation, avatar->M);
			avatar->animate(tex, deltaT);
			
			renderTracker(renderTrackerOrTargetPosition);
			
			VrInterface::endRender(eye);
		}
		
		VrInterface::warpSwap();
		
		Graphics4::restoreRenderTarget();
		Graphics4::clear(Graphics4::ClearColorFlag | Graphics4::ClearDepthFlag, Graphics1::Color::Black, 1.0f, 0);
		
		// Render on Monitor
		if (!firstPersonMonitor) {
			// Camera view
			mat4 P = getProjectionMatrix();
			mat4 V = getViewMatrix();
			
			Graphics4::setMatrix(vLocation, V);
			Graphics4::setMatrix(pLocation, P);
		} else {
			// First person view
			Graphics4::setMatrix(vLocation, state.pose.vrPose.eye);
			Graphics4::setMatrix(pLocation, state.pose.vrPose.projection);
		}
		Graphics4::setMatrix(mLocation, avatar->M);
		avatar->animate(tex, deltaT);
		
		renderTracker(renderTrackerOrTargetPosition);

		Graphics4::setPipeline(pipeline);
		
		//cube->drawVertices(cube->M, state.pose.vrPose.eye, state.pose.vrPose.projection, width, height);
		//avatar->drawJoints(avatar->M, state.pose.vrPose.eye, state.pose.vrPose.projection, width, height, true);
		
#else
		if (!initCharacter) {
			avatar->setScale(0.929);	// Scale test
			
			if (readData) {
				log(Info, "Read data from file %s", initialTransFilename);
				vec3 initPos = vec3(0, 0, 0);
				logger->readInitTransAndRot(initialTransFilename, &initPos, &initRot);
				initTrans = mat4::Translation(initPos.x(), initPos.y(), initPos.z());
				
				cameraRotation.rotate(Quaternion(vec3(0, 1, 0), -Kore::pi/2));
				cameraPosition = vec3(0.8, 0.8, 1.8);
				
				line = 500;
			}
			
			//rotate hands
			initDesRotationLeftHand.rotate(Quaternion(vec3(0, 1, 0), -Kore::pi / 2));
			initDesRotationRightHand.rotate(Quaternion(vec3(0, 1, 0), Kore::pi / 2));
			
			initRot.normalize();
			initRotInv = initRot.invert();
			
			avatar->M = initTrans * initRot.matrix().Transpose();
			initTransInv = (initTrans * initRot.matrix().Transpose()).Invert();

			initCharacter = true;
			
			if (logData) {
				vec4 initPos = initTrans * vec4(0, 0, 0, 1);
				logger->saveInitTransAndRot(vec3(initPos.x(), initPos.y(), initPos.z()), initRot);
			}
		}
		
		if (readData) {
			Kore::vec3 rawPos = vec3(0, 0, 0);
			Kore::Quaternion rawRot = Kore::Quaternion(0, 0, 0, 1);

			if (logger->readData(line, positionDataFilename, &rawPos, &rawRot)) {
				desPosition1 = rawPos;
				desRotation1 = rawRot;
				
				//log(Info, "pos %f %f %f rot %f %f %f %f", desPosition1.x(), desPosition1.y(), desPosition1.z(), desRotation1.x, desRotation1.y, desRotation1.z, desRotation1.w);
				
				setDesiredPositionAndOrientation(desPosition1, desRotation1, leftHandBoneIndex);
			}

			//log(Info, "%i", line);
			++line;
		} else {
			angle += 0.01;
			float radius = 0.2;
			
			// Set foot position
			//desPosition2 = vec3(-0.2 + radius * Kore::cos(angle), 0.3 + radius * Kore::sin(angle), 0.2);
			//setDesiredPosition(desPosition2, 53); // Left foot 49, right foot 53
			
			// Set hand position
			radius = 0.1;
			//desPosition = vec3(0.2 + radius * Kore::cos(angle), 0.9 + radius * Kore::sin(angle), 0.2);
			//desPosition = vec3(0.2 + radius * Kore::cos(angle), 0.9, 0.2);
			
			
			// Set position and orientation for the left hand
			desPosition1 = vec3(0.2, 1.0, 0.4);
			desRotation1 = Quaternion(vec3(1, 0, 0), Kore::pi/2);
			desRotation1.rotate(Quaternion(vec3(0, 1, 0), -angle));
			setDesiredPositionAndOrientation(desPosition1, desRotation1, leftHandBoneIndex);
			
			// Set position and orientation for the right hand
			//desPosition2 = (-0.2, 1.0, 0.4);
			//desRotation2 = Quaternion(vec3(1, 0, 0), Kore::pi/2);
			//desRotation2.rotate(Quaternion(vec3(0, 1, 0), angle));
			//setDesiredPositionAndOrientation(desPosition2, desRotation2, rightHandBoneIndex);
			
			logger->saveLogData("angle", angle);
		}
		
		// projection matrix
		mat4 P = getProjectionMatrix();
		
		// view matrix
		mat4 V = getViewMatrix();
		
		Graphics4::setMatrix(vLocation, V);
		Graphics4::setMatrix(pLocation, P);
		
		Graphics4::setMatrix(mLocation, avatar->M);
		
		avatar->animate(tex, deltaT);
		
		//cube->drawVertices(cube->M, V, P, width, height);
		//avatar->drawJoints(avatar->M, V, P, width, height, true);
		
		/*Quaternion q1 = avatar->getBoneLocalRotation(leftHandBoneIndex-1);
		Quaternion q2 = avatar->getBoneLocalRotation(leftHandBoneIndex-2);
		log(Info, "low %f %f %f %f", q1.w, q1.x, q1.y, q1.z);
		log(Info, "up %f %f %f %f", q2.w, q2.x, q2.y, q2.z);*/
		
		renderTracker();
		Graphics4::setPipeline(pipeline);
#endif
		
		
		Graphics4::end();
		Graphics4::swapBuffers();
	}
	
	void keyDown(KeyCode code) {
		switch (code) {
			case Kore::KeyLeft:
			case Kore::KeyA:
				left = true;
				break;
			case Kore::KeyRight:
			case Kore::KeyD:
				right = true;
				break;
			case Kore::KeyDown:
				down = true;
				break;
			case Kore::KeyUp:
				up = true;
				break;
			case Kore::KeyW:
				forward = true;
				break;
			case Kore::KeyS:
				backward = true;
				break;
			case Kore::KeyX:
				rotateX = true;
				break;
			case Kore::KeyY:
				rotateY = true;
				break;
			case Kore::KeyZ:
				rotateZ = true;
				break;
			case Kore::KeyR:
#ifdef KORE_STEAMVR
				VrInterface::resetHmdPose();
#endif
				break;
			case KeyL:
				Kore::log(Kore::LogLevel::Info, "Position: (%f, %f, %f)", cameraPosition.x(), cameraPosition.y(), cameraPosition.z());
				Kore::log(Kore::LogLevel::Info, "Rotation: (%f, %f, %f %f)", cameraRotation.w, cameraRotation.x, cameraRotation.y, cameraRotation.z);
				break;
			case KeyQ:
				System::stop();
				break;
			default:
				break;
		}
	}
	
	void keyUp(KeyCode code) {
		switch (code) {
			case Kore::KeyLeft:
			case Kore::KeyA:
				left = false;
				break;
			case Kore::KeyRight:
			case Kore::KeyD:
				right = false;
				break;
			case Kore::KeyDown:
				down = false;
				break;
			case Kore::KeyUp:
				up = false;
				break;
			case Kore::KeyW:
				forward = false;
				break;
			case Kore::KeyS:
				backward = false;
				break;
			case Kore::KeyX:
				rotateX = false;
				break;
			case Kore::KeyY:
				rotateY = false;
				break;
			case Kore::KeyZ:
				rotateZ = false;
				break;
			default:
				break;
		}
	}
	
	void mouseMove(int windowId, int x, int y, int movementX, int movementY) {
		float rotationSpeed = 0.01;
		if (rotateX) {
			//cameraRotation.x() += (float)((mousePressX - x) * rotationSpeed);
			cameraRotation.rotate(Quaternion(vec3(0, 1, 0), (float)((mousePressX - x) * rotationSpeed)));
			mousePressX = x;
		} else if (rotateZ) {
			cameraRotation.rotate(Quaternion(vec3(1, 0, 0), (float)((mousePressY - y) * rotationSpeed)));
			mousePressY = y;
		}
	}
	
	void mousePress(int windowId, int button, int x, int y) {
		//rotateX = true;
		//rotateZ = true;
		mousePressX = x;
		mousePressY = y;
	}
	
	void mouseRelease(int windowId, int button, int x, int y) {
		//rotateX = false;
		//rotateZ = false;
	}
	
	void init() {
		FileReader vs("shader.vert");
		FileReader fs("shader.frag");
		//FileReader vs("shader_lighting.vert");
		//FileReader fs("shader_lighting.frag");
		vertexShader = new Shader(vs.readAll(), vs.size(), VertexShader);
		fragmentShader = new Shader(fs.readAll(), fs.size(), FragmentShader);
		
		// This defines the structure of your Vertex Buffer
		structure.add("pos", Float3VertexData);
		structure.add("tex", Float2VertexData);
		structure.add("nor", Float3VertexData);
		
		pipeline = new PipelineState;
		pipeline->inputLayout[0] = &structure;
		pipeline->inputLayout[1] = nullptr;
		pipeline->vertexShader = vertexShader;
		pipeline->fragmentShader = fragmentShader;
		pipeline->depthMode = ZCompareLess;
		pipeline->depthWrite = true;
		pipeline->blendSource = Graphics4::SourceAlpha;
		pipeline->blendDestination = Graphics4::InverseSourceAlpha;
		pipeline->alphaBlendSource = Graphics4::SourceAlpha;
		pipeline->alphaBlendDestination = Graphics4::InverseSourceAlpha;
		pipeline->compile();
		
		tex = pipeline->getTextureUnit("tex");
		
		pLocation = pipeline->getConstantLocation("P");
		vLocation = pipeline->getConstantLocation("V");
		mLocation = pipeline->getConstantLocation("M");
		
		cube1 = new MeshObject("cube.ogex", "", structure, 0.05);
		cube2 = new MeshObject("cube.ogex", "", structure, 0.05);
		cube3 = new MeshObject("cube.ogex", "", structure, 0.05);
		cube4 = new MeshObject("cube.ogex", "", structure, 0.05);
#ifdef KORE_STEAMVR
		avatar = new MeshObject("avatar/avatar_skeleton_headless.ogex", "avatar/", structure);
		cameraRotation.rotate(Quaternion(vec3(0, 1, 0), Kore::pi));
#else
		avatar = new MeshObject("avatar/avatar_skeleton.ogex", "avatar/", structure);
#endif
		initRot.rotate(Quaternion(vec3(1, 0, 0), -Kore::pi / 2.0));
		
		Graphics4::setTextureAddressing(tex, Graphics4::U, Repeat);
		Graphics4::setTextureAddressing(tex, Graphics4::V, Repeat);
		
		logger = new Logger();
		
#ifdef KORE_STEAMVR
		VrInterface::init(nullptr, nullptr, nullptr); // TODO: Remove
#endif
	}
	
}

int kore(int argc, char** argv) {
	System::init("BodyTracking", width, height);
	
	init();
	
	System::setCallback(update);
	
	startTime = System::time();
	
	Keyboard::the()->KeyDown = keyDown;
	Keyboard::the()->KeyUp = keyUp;
	Mouse::the()->Move = mouseMove;
	Mouse::the()->Press = mousePress;
	Mouse::the()->Release = mouseRelease;
	
	System::start();
	
	return 0;
}
