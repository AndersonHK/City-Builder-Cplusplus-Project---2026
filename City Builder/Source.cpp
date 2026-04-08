#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <ctime>
#include <stdlib.h>

#include "shader.cpp"
#include "lotModule.h"
#include "square.h"
#include <mutex>

using namespace std;

//Test module
lotModule blockedLotModule = lotModule(1, 1, 0, 0);
lotModule smokeStack = lotModule(1, 1, 10000, -1000);
lotModule parkTwoByTwo = lotModule(2, 2, -4000, 10000);

class lot {
public:
	square* squaresOwned[16];
	lotModule* modulesBuilt[16];
	int squaresUsed = 0;
	int modulesUsed = 0;
	int airPollutionEmit = 0;
	int landValueEmit = 0;
	//unsigned int pointer = 0;
	lot(square* assignSquares[], lotModule* assignModules[], int length);
	lot();
	void localUpdate();
};
lot::lot() {}
lot::lot(square* assignSquares[], lotModule* assignModules[], int length)
{
	//lot lots[10] = *(pointer);
	//lots[lotsInUse].store(*this);
	//lotsInUse.fetch_add(1);
	for (int i = 0; i < length; i++)
	{
		squaresOwned[i] = assignSquares[i];
		modulesBuilt[i] = assignModules[i];
		squaresUsed++;
		modulesUsed++;
		airPollutionEmit += modulesBuilt[i]->airPollutionEmit;
		landValueEmit += modulesBuilt[i]->landValueEmit;
		squaresOwned[i]->isVacant = false;
	}
}
void lot::localUpdate()
{
	for (int i = 0; i < squaresUsed; i++) {
		squaresOwned[i]->airPolution = squaresOwned[i]->airPolution + airPollutionEmit / squaresUsed;
		squaresOwned[i]->landValue = squaresOwned[i]->landValue + landValueEmit / squaresUsed;
	}
}

atomic<int> phase = 0; //start with calculating square values
atomic<int> thread_done = 0;
atomic<int> thread_started = 0;
mutex mtx; //New Thread Pool Lock System
condition_variable cv;
atomic<int> fps = 0;

const int mapLen = 1024;
const int threadcount = 12; // Make it higher than the number of CPU threads, loads split unevenly between threads

square squares[mapLen][mapLen];
square Osquares[mapLen][mapLen];

atomic<int> lotsInUse = 0;

//Important list of lots!!!
lot lots[100000];

//Test lots
void makeLot(lotModule* modules_to_add[], square* squares_to_add[]) {
	lot newLot = lot(squares_to_add, modules_to_add, 2);
	lots[lotsInUse] = newLot;
	lotsInUse++;
	/*cout << lotsInUse << " this is how many lots are in use." << endl;
	for (int i = 0; i < lotsInUse; i++) {
		lot TheLot = lots[i];
		int toPrint = TheLot.landValueEmit;
		cout << toPrint << " this is the landValueEmit of lot " << i << endl;
	}*/
}

void makeMap(int partition) {
	for (int x = mapLen*((partition - 1) / threadcount); x < mapLen * partition / threadcount; x++) {
		for (int y = 0; y < mapLen; y++) {
			squares[x][y].x.store(x, memory_order_relaxed);
			squares[x][y].y.store(y, memory_order_relaxed);
			Osquares[x][y].x.store(x, memory_order_relaxed);
			Osquares[x][y].y.store(y, memory_order_relaxed);
			squares[x][y].airPolution.store(rand() * 10, memory_order_relaxed);
			squares[x][y].landValue.store(rand() * 10, memory_order_relaxed);
		}
	}
}

void localUpdateSquares(int partition) {
	for (int x = mapLen*(((float)partition - 1) / threadcount); x < mapLen * (float)partition / threadcount; x++) {
		for (int y = 0; y < mapLen; y++) {
			squares[x][y].localUpdate();
		}
	}
}

const int pollutionSpreadRate = 4; //lower, the faster, at 2 you have instant equalization, at 1 you have flipping squares, at 0 it crashes

void updateNeighbor(int x, int y, int xd, int yd, long int airPolution, long int *airPolutionDelta, long int landValue, long int *landValueDelta) {
	*airPolutionDelta += (Osquares[(x + xd)][(y + yd)].airPolution.load(memory_order_relaxed) - airPolution) / pollutionSpreadRate;
	*landValueDelta += (Osquares[(x + xd)][(y + yd)].landValue.load(memory_order_relaxed) - landValue) / pollutionSpreadRate;
}

void neighborUpdateSquares(int partition) {
	long int airPolution = 0;
	long int airPolutionDelta = 0;
	long int landValue = 0;
	long int landValueDelta = 0;
	for (int x = mapLen*(((float)partition - 1) / threadcount); x < mapLen * (float)partition / threadcount; x++) {
		for (int y = 0; y < mapLen; y++) {
			airPolution = squares[x][y].airPolution.load(memory_order_relaxed);
			landValue = squares[x][y].landValue.load(memory_order_relaxed);
			airPolutionDelta = 0;
			landValueDelta = 0;
			if (x < mapLen - 1) { //Left Update
				updateNeighbor(x, y, 1, 0, airPolution, &airPolutionDelta, landValue, &landValueDelta);
			}
			if (x > 0) { //Right update
				updateNeighbor(x, y, -1, 0, airPolution, &airPolutionDelta, landValue, &landValueDelta);
			}
			if (y < mapLen - 1) { //Up Update
				updateNeighbor(x, y, 0, 1, airPolution, &airPolutionDelta, landValue, &landValueDelta);
			}
			if (y > 0) { //Down update
				updateNeighbor(x, y, 0, -1, airPolution, &airPolutionDelta, landValue, &landValueDelta);
			}
			squares[(x)][y].airPolution.store(airPolutionDelta + airPolution, memory_order_relaxed);
			squares[(x)][y].landValue.store(landValueDelta + landValue, memory_order_relaxed);
		}
	}
}

void localUpdateLots(int partition) {
	for (int x = lotsInUse*(((float)partition - 1) / threadcount); x < lotsInUse * (float)partition / threadcount; x++) {
		lots[x].localUpdate();
	}
}

void updateRecords(int partition) {
	for (int x = mapLen*(((float)partition - 1) / threadcount); x < mapLen * (float)partition / threadcount; x++) {
		for (int y = 0; y < mapLen; y++) {
			Osquares[x][y].Update(squares[x][y].landValue.load(memory_order_relaxed), squares[x][y].airPolution.load(memory_order_relaxed), squares[x][y].isVacant.load(memory_order_relaxed), squares[x][y].zoningType.load(memory_order_relaxed));
		}
	}
}

void task1(long long int testvar, int thread_number)
{
	thread_started++;
	for (; ; ) {
		if (phase.load() != -1) {
			if (phase.load() == 0) {
				makeMap(thread_number + 1);
				//cout << "Phase 0" << endl;
			}
			else if (phase.load() == 1) {
				updateRecords(thread_number + 1);
			}
			else if (phase.load() == 4) {
				localUpdateSquares(thread_number + 1);
				//cout << "Phase 4" << endl;
			}
			else if (phase.load() == 2) {
				neighborUpdateSquares(thread_number + 1);
			}
			else if (phase.load() == 3) {
				localUpdateLots(thread_number + 1);
			}
			unique_lock<std::mutex> lck(mtx);
			thread_done++;
			cv.wait(lck);
		}
		else {
			return;
		}
	}
}

bool buttonPressed = false;
int keyPressed = 81;
double mouseX = 0;
double mouseY = 0;
double camX = 0;
double camY = 0;
const float max_zoom = 512;
const float min_zoom = 128;
float zoom = 256;
//bool cursorInScreen = true;

//User input management
static void cursorPositionCallback(GLFWwindow* window, double xpos, double ypos) {
	mouseX = xpos; mouseY = ypos;
	int x = (int)xpos / 4 * zoom / max_zoom + camX;
	int y = zoom -(int)ypos / 4 * zoom / max_zoom + camY;
	if (buttonPressed && xpos<2048 && ypos<2048) {
		if (keyPressed == 81) {
			squares[x][y].airPolution.store(16000000, memory_order_seq_cst);
		}
	}
}
static void placeBuilding(lotModule* modules_to_add[], square* squares_to_add[], int size) {
	bool goAhead = true;
	for (int i = 0; i < size; i++) {
		if (squares_to_add[i]->isVacant == false) {
			goAhead = false;
			break;
		}
	}
	if (goAhead) {
		makeLot(modules_to_add, squares_to_add);
	}
	else {
		cout << "lot is occupied..." << endl;
	}
}

static void glfwSetMouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
	if (button == GLFW_MOUSE_BUTTON_1 && action == GLFW_PRESS) {
		buttonPressed = true;

		if (keyPressed == 87) {
			int x = (int)mouseX / 4 * zoom / max_zoom + camX;
			int y = zoom - (int)mouseY / 4 * zoom / max_zoom + camY;
			int size = 2;
			lotModule* modules_to_add[] = { &smokeStack, &smokeStack };
			square* squares_to_add[] = { &squares[x][y], &squares[x][y - 1] };
			placeBuilding(modules_to_add, squares_to_add, size);
		}
		else if (keyPressed == 69) {
			int x = (int)mouseX / 4 * zoom / max_zoom + camX;
			int y = zoom - (int)mouseY / 4 * zoom / max_zoom + camY;
			int size = 4;
			lotModule* modules_to_add[] = { &parkTwoByTwo, &blockedLotModule,&blockedLotModule,&blockedLotModule };
			square* squares_to_add[] = { &squares[x][y], &squares[x][y - 1],&squares[x-1][y], &squares[x-1][y - 1] };
			placeBuilding(modules_to_add, squares_to_add, size);
		}
		else if (keyPressed == 65) {
			int x = (int)mouseX / 4 * zoom / max_zoom + camX;
			int y = zoom - (int)mouseY / 4 * zoom / max_zoom + camY;
			cout << "Query tool result: " << " square " << x << "x " << y << "y has a land value of " << Osquares[x][y].landValue.load() << " and a pollution of " << Osquares[x][y].airPolution.load() << endl;
		}
		
	}
	else if (button == GLFW_MOUSE_BUTTON_1 && action == GLFW_RELEASE) {
		buttonPressed = false;
	}
}
static void glfwSetKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	if (action == GLFW_PRESS || GLFW_REPEAT) {
		switch (key) {
			case 262:
				//cout << "moving right cause key pressed is " << key << endl;
				if (camX < mapLen - (double) zoom - (zoom / min_zoom)) {
					camX = camX + (zoom / min_zoom);
				}
				return;
			case 263:
				//cout << "moving left cause key pressed is " << key << endl;
				if (camX > (zoom / min_zoom)) {
					camX = camX - (zoom / min_zoom);
				}
				return;
			case 264:
				//cout << "moving up cause key pressed is " << key << endl;
				if (camY > (zoom / min_zoom)) {
					camY = camY - (zoom / min_zoom);
				}
				return;
			case 265:
				//cout << "moving down cause key pressed is " << key << endl;
				if (camY < mapLen - (double)zoom - (zoom / min_zoom)) {
					camY = camY + (zoom / min_zoom);
				}
				return;
		}
		keyPressed = key;
		cout << key << endl;
	}
}
static void glfwSetScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
	if (yoffset < 0 && zoom < max_zoom) {
		zoom = zoom * 2;
		camX = camX - zoom / 4;
		camY = camY - zoom / 4;
		if (camX < 0) { camX = 0; }
		if (camY < 0) { camY = 0; }
		cout << "zooming out because scrolling at " << yoffset << endl;
		cout << "zoom level at " << zoom << endl;
		cout << "x and y at " << camX << ", " << camY << endl;
	}
	else if (yoffset > 0 && zoom > min_zoom) {
		zoom = zoom / 2;
		camX = camX + zoom / 2;
		camY = camY + zoom / 2;
		cout << "zooming in because scrolling at " << yoffset << endl;
		cout << "zoom level at " << zoom << endl;
		cout << "x and y at " << camX << ", " << camY << endl;
	}
}
/*
static void glfwSetCursorEnterCallback(GLFWwindow* window, int entered) {
	if (entered) {
		cursorInScreen = true;
	}
	else {
		cursorInScreen = false;
	}
}*/


void startwindow() {

	GLFWwindow* window;

	/* Initialize the library */
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	/* Create a windowed mode window and its OpenGL context */
	window = glfwCreateWindow(2048, 2048, "Project Prime", NULL, NULL);
	if (!window)
	{
		glfwTerminate();
	}

	/* Make the window's context current */
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);

	/* Initialize the OpenGL library */
	glewInit();

	float positions[] = {
		-0.5f, -0.5f,
		0.5f, -0.5f,
		0.5f,  0.5f,

		0.5f,  0.5f,
		-0.5f,  0.5f,
		-0.5f, -0.5f
	};

	//Logic for texture transparency
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	unsigned int vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	unsigned int buffer;
	glGenBuffers(1, &buffer);
	glBindBuffer(GL_ARRAY_BUFFER, buffer);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, 0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (const void *)8);

	//PreCompile Shader?
	ShaderProgramSource source = ParseShader("Basic.shader");

	unsigned int shader = CreateShader(source.VertexSource, source.FragmentSource);
	glUseProgram(shader);

	//int location = glGetUniformLocation(shader, "u_Color");

	float tempValue = 0.0;
	float tempPolution = 0.0;
	float red = 1.0;
	float green = 0.0;
	const int imax_zoom = 512; //Max_zoom issue with const

	GLsizei arrayOfPOS[imax_zoom * 2];
	GLsizei arrayOfStarts[imax_zoom * 2];
	for (int x = 0; x < imax_zoom * 2; x++) {
		arrayOfPOS[x] = 6;
		arrayOfStarts[x] = x * 6;
	}
	int i;
	float positionsToDraw[24* imax_zoom];
	//float all_positionsToDraw[24 * imax_zoom * 2];

	//Setup mouse
	glfwSetCursorPosCallback(window, cursorPositionCallback);
	glfwSetMouseButtonCallback(window, glfwSetMouseButtonCallback);
	glfwSetKeyCallback(window, glfwSetKeyCallback);
	glfwSetScrollCallback(window, glfwSetScrollCallback);
	//glfwSetCursorEnterCallback(window, glfwSetCursorEnterCallback);

	//Setting up lot propreties
	float lotPositionsToDraw[24];
	int xSize;
	int ySize;
	int xPos;
	int yPos;

	int frames = 0;
	time_t stime; // Making time
	time(&stime);
	time_t etime; // Making time

	/* Loop until the user closes the window */
	while (!glfwWindowShouldClose(window))
	{
		time(&etime);
		if (difftime(etime, stime) > 1) {
			cout << "FPS " << frames << " Fixed Update: " << fps << endl;
			frames = 0;
			time(&stime);
		}
		glClear(GL_COLOR_BUFFER_BIT);
		frames++;

		i = 0; //Multidraw logic

		if (phase == -1) {
			return;
		}

		for (int x = 0; x < zoom; x++) {
			for (int y = 0; y < zoom; y++) {

				positionsToDraw[0 + (24 * y)] = positions[0] / zoom * 2 + x / zoom * 2 - 1.0; //-
				positionsToDraw[1 + (24 * y)] = positions[1] / zoom * 2 + y / zoom * 2 - 1.0; //-

				positionsToDraw[4 + (24 * y)] = positions[2] / zoom * 2 + x / zoom * 2 - 1.0; //+
				positionsToDraw[5 + (24 * y)] = positions[3] / zoom * 2 + y / zoom * 2 - 1.0; //-

				positionsToDraw[8 + (24 * y)] = positions[4] / zoom * 2 + x / zoom * 2 - 1.0; //+
				positionsToDraw[9 + (24 * y)] = positions[5] / zoom * 2 + y / zoom * 2 - 1.0; //+

				positionsToDraw[12 + (24 * y)] = positionsToDraw[8 + (24 * y)]; //+
				positionsToDraw[13 + (24 * y)] = positionsToDraw[9 + (24 * y)]; //+

				positionsToDraw[16 + (24 * y)] = positions[8] / zoom * 2 + x / zoom * 2 - 1.0; //-
				positionsToDraw[17 + (24 * y)] = positions[9] / zoom * 2 + y / zoom * 2 - 1.0; //+

				positionsToDraw[20 + (24 * y)] = positionsToDraw[0 + (24 * y)]; //-
				positionsToDraw[21 + (24 * y)] = positionsToDraw[1 + (24 * y)]; //-

				tempValue = Osquares[x+(int)camX][y + (int)camY].landValue.load(memory_order_relaxed);
				tempPolution = Osquares[x+(int)camX][y + (int)camY].airPolution.load(memory_order_relaxed);
				red = 0.5 + (tempPolution / 1280000);
				green = 0.5 + (tempValue / 1280000);
				//glUniform4f(location, red, green, 0.0, 1.0);

				positionsToDraw[2 + (24 * y)] = red;
				positionsToDraw[3 + (24 * y)] = green;

				positionsToDraw[6 + (24 * y)] = red;
				positionsToDraw[7 + (24 * y)] = green;

				positionsToDraw[10 + (24 * y)] = red;
				positionsToDraw[11 + (24 * y)] = green;

				positionsToDraw[14 + (24 * y)] = red;
				positionsToDraw[15 + (24 * y)] = green;

				positionsToDraw[18 + (24 * y)] = red;
				positionsToDraw[19 + (24 * y)] = green;

				positionsToDraw[22 + (24 * y)] = red;
				positionsToDraw[23 + (24 * y)] = green;

			}
			glBufferData(GL_ARRAY_BUFFER, imax_zoom * 24 * sizeof(float), positionsToDraw, GL_DYNAMIC_DRAW);
			glMultiDrawArrays(GL_TRIANGLES, arrayOfStarts, arrayOfPOS, imax_zoom);/*
			if ((x+1) % 2 == 0) {
				memcpy(all_positionsToDraw + _countof(positionsToDraw), positionsToDraw, sizeof(positionsToDraw));
				glBufferData(GL_ARRAY_BUFFER, mapLen * 2 * 24 * sizeof(float), all_positionsToDraw, GL_DYNAMIC_DRAW);
				glMultiDrawArrays(GL_TRIANGLES, arrayOfStarts, arrayOfPOS, mapLen * 2);
			}
			else{
				memcpy(all_positionsToDraw, positionsToDraw, sizeof(positionsToDraw));
			}*/
		}

		//Draw lots now
		for (int l = 0; l < lotsInUse; l++) {
			for (int m = 0; m < lots[l].modulesUsed; m++) {
				xSize = lots[l].modulesBuilt[m]->x*2;
				ySize = lots[l].modulesBuilt[m]->y*2;
				xPos = lots[l].squaresOwned[m]->x - (int)camX;
				yPos = lots[l].squaresOwned[m]->y - (int)camY;
				if (xPos < 0 || yPos < 0) { break; }

				lotPositionsToDraw[0 + (24 * i)] = positions[0] / zoom * xSize + xPos / zoom * 2 - 1.0; //-
				lotPositionsToDraw[1 + (24 * i)] = positions[1] / zoom * ySize + yPos / zoom * 2 - 1.0; //-

				lotPositionsToDraw[4 + (24 * i)] = positions[2] / zoom * xSize + xPos / zoom * 2 - 1.0; //+
				lotPositionsToDraw[5 + (24 * i)] = positions[3] / zoom * ySize + yPos / zoom * 2 - 1.0; //-

				lotPositionsToDraw[8 + (24 * i)] = positions[4] / zoom * xSize + xPos / zoom * 2 - 1.0; //+
				lotPositionsToDraw[9 + (24 * i)] = positions[5] / zoom * ySize + yPos / zoom * 2 - 1.0; //+

				lotPositionsToDraw[12 + (24 * i)] = lotPositionsToDraw[8 + (24 * i)]; //+
				lotPositionsToDraw[13 + (24 * i)] = lotPositionsToDraw[9 + (24 * i)]; //+

				lotPositionsToDraw[16 + (24 * i)] = positions[8] / zoom * xSize + xPos / zoom * 2 - 1.0; //-
				lotPositionsToDraw[17 + (24 * i)] = positions[9] / zoom * ySize + yPos / zoom * 2 - 1.0; //+

				lotPositionsToDraw[20 + (24 * i)] = lotPositionsToDraw[0 + (24 * i)]; //-
				lotPositionsToDraw[21 + (24 * i)] = lotPositionsToDraw[1 + (24 * i)]; //-

				//tempValue = Osquares[x][y].landValue.load();
				red = 0;
				green = 0;
				//glUniform4f(location, red, green, 0.0, 1.0);

				lotPositionsToDraw[2 + (24 * i)] = red;
				lotPositionsToDraw[3 + (24 * i)] = green;

				lotPositionsToDraw[6 + (24 * i)] = red;
				lotPositionsToDraw[7 + (24 * i)] = green;

				lotPositionsToDraw[10 + (24 * i)] = red;
				lotPositionsToDraw[11 + (24 * i)] = green;

				lotPositionsToDraw[14 + (24 * i)] = red;
				lotPositionsToDraw[15 + (24 * i)] = green;

				lotPositionsToDraw[18 + (24 * i)] = red;
				lotPositionsToDraw[19 + (24 * i)] = green;

				lotPositionsToDraw[22 + (24 * i)] = red;
				lotPositionsToDraw[23 + (24 * i)] = green;

				glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(float), lotPositionsToDraw, GL_DYNAMIC_DRAW);
				glDrawArrays(GL_TRIANGLES, 0, 6);
			}
		}

		/* Render here */
		//glClear(GL_COLOR_BUFFER_BIT);
		//glDrawArrays(GL_TRIANGLES, 0, 6);
		/* Swap front and back buffers */
		glfwSwapBuffers(window);

		/* Poll for and process events */
		glfwPollEvents();
	}
	glDeleteProgram(shader);
	glfwTerminate();
	phase.store(-1);
}

vector<thread> spawnThreads(int n, long long int testvar)
{
	vector<thread> threads(n);
	// spawn n threads:
	for (int i = 0; i < n; i++) {
		threads[i] = thread(task1, testvar, i);
	}
	return threads;
}

int main() {
	//Creating Window with Renderer and OpenGL Context
	thread renderthread(startwindow);

	//Setting Up Worker Threads
	long long int testvar = 0;
	int threadone_private = 0;
	vector<thread> threads = spawnThreads(threadcount, testvar);

	//Handling timming
	int fixed_update = 120;
	time_t stime; // Making time
	time(&stime);
	time_t etime; // Making time
	int frames = 0;
	srand(time(NULL)); //Initializing randomness

	//Give default value to squares for test
	//makeMap(squares);

	for (; ; ) {
		while (threadcount != thread_started){}
		while (testvar < 1000000000000) {
			time(&etime);
			if (difftime(etime, stime) > 1) {
				fps = frames;
				frames = 0;
				time(&stime);
			}
			if (phase == -1) {
				break;
			}
			if (thread_done == threadcount) {
				//cout << "Waiting for Lock in Main Thread" << endl;
				//cout << thread_done << endl;
				unique_lock<mutex> lck(mtx);
				if (phase == 4) {
					phase = 1;
					frames++;
					testvar++;
				}
				else {
					phase++;
				}
				thread_done.store(0);
				//cout << thread_done << endl;
				cv.notify_all();
				//cout << "Got Lock in Main Thread" << endl;
			}
		} // Cleanup
		phase.store(-1);
		break;
	}
	cout << "The Program has Finished, exit now." << endl;
	for (int i = 0; i < threadcount; i++) {
		if (renderthread.joinable()) {
			renderthread.join();
		}
		cv.notify_all();
		threads[i].join();
	}
	return 0;
}