#include "stdafx.h"
#include "MapCollision.h"
#include "Player.h"
#include <sstream>
#include <iomanip>
#include "App.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <shlobj.h>   // SHCreateDirectoryExA

#define ATTR_WIDTH 256
#define ATTR_HEIGHT 256
#define HEADER_SIZE 6

MapCollision::MapCollision(const char* map_name){
	maxX = 0;
	maxY = 0;
	// ~MapCollision frees/deletes all four unconditionally, and the !val path below returns before the
	// three pathfinding members are ever assigned -- CBackground deletes us on every map change.
	map = 0;
	anyAngleMap = 0;
	aPathPlaning = 0;
	pathFinding = 0;
	mapName = std::string(map_name);
	// No existence probe first: loadMapFromDisk already fails cleanly on a missing .dat, so probing
	// only bought a second open of the same file -- on every world re-entry.
	bool val = loadMapFromDisk();
	if (!val) {
		val = constructMapFromClient();
		if (val)
			saveMap();
	}
	if(val)
		LOG_INFO("Current map %s loaded with success! Max-X: %d  Max-Y: %d",map_name,maxX,maxY);
	if (!val) {
		LOG_ERROR("Error constructing map! Functions that need map information  will not work");
		return;
	}

	anyAngleMap = new std::vector<bool>(map, map + (maxY * maxX));
	aPathPlaning = new ANYA(*anyAngleMap, maxX, maxY); //Using ANYA because all the others run into error on debug mode
	pathFinding = new JPS::Searcher<MapCollision>(*this);
}


MapCollision::~MapCollision()
{
	free(map);
	delete aPathPlaning;
	delete anyAngleMap;
	delete pathFinding;
}

inline bool MapCollision::isBlocked(int x, int y){
	if (x < 0 || y < 0 || x >= maxX || y >= maxY) {
		return true;
	}
	return !(bool)map[y * maxX + x];
	//return map[y*maxX + x] & 0x0;
}


bool MapCollision::findPath(int x_start, int y_start, int x_end, int y_end, std::vector<Point>& path)
{
	JPS::PathVector pathBuf;
	std::vector<xyLoc> finalPath;


	//This is much faster so it is used to check if a destination is even possible
	bool found = pathFinding->findPath(pathBuf, JPS::Pos(x_start, y_start), JPS::Pos(x_end, y_end), 0);


	if  (found) {
		auto length = aPathPlaning->FindXYLocPath({ (uint16_t)x_start,(uint16_t)y_start }, { (uint16_t)x_end,(uint16_t)y_end }, finalPath);
		LOG_DEBUG("Path found from (%d,%d) to (%d,%d) with %d points!", x_start, y_start, x_end, y_end, finalPath.size());
	}
	else {
		LOG_DEBUG("No Path from (%d,%d) to (%d,%d)!!!", x_start, y_start, x_end, y_end);
	}

	if (found) {

		for (auto point : finalPath) {
			path.push_back(Point(point.x, point.y));
		}
		/*
		for (JPS::PathVector::iterator it = pathBuf.begin(); it != pathBuf.end(); ++it)
			path.push_back(Point(it->x, it->y));*/
	}
	return found;

}

inline unsigned MapCollision::operator()(unsigned x, unsigned y) const
{
	if (x < 0 || y < 0 || x >= maxX || y >= maxY) {
		return true;
	}
	return map[y*maxX + x];
}



bool MapCollision::fileExists(const char * file)
{
	PyObject * mod = PyImport_ImportModule("app");
	long ret = 0;

	// CallMethodRetLong builds+owns the (s) args tuple internally -> no manual refcount to double-free.
	if (CallMethodRetLong(mod, "IsExistFile", &ret, "(s)", file)) {
		Py_DECREF(mod);
		return ret != 0;
	}
	Py_DECREF(mod);
	return false;
}

bool MapCollision::constructMapFromClient()
{
	std::vector<MapPiece*> buffer;
	// The client reads .atr from GetMapDataDirectory() = the (short) map name for normal maps. But copied
	// dungeon instances use the PARENT name, and some client setups resolve maps under the ymir-work root.
	// Try candidate base paths and use whichever the client's Eter (fileExists = app.IsExistFile) has.
	const char* prefixes[] = { "", "d:/ymir work/map/", "map/", "d:\\ymir work\\map\\" };
	std::string baseFolder;
	bool baseFound = false;
	for (int pi = 0; pi < 4; pi++) {
		std::string cand = std::string(prefixes[pi]) + mapName + "\\";
		std::string test = cand + "000000\\attr.atr";
		bool ex = fileExists(test.c_str());
		if (ex) { baseFolder = cand; baseFound = true; break; }
	}
	if (!baseFound) {
		return false;
	}
	int counter = 0;
	int xPieces = 0;
	int largestYPiece = 1;
	for (int x = 0; ; x++, xPieces++) {
		std::ostringstream x_folder;
		x_folder << std::setw(3) << std::setfill('0') << x;
		std::string firstPiece = baseFolder + x_folder.str();
		firstPiece += "000\\attr.atr";
		if (!fileExists(firstPiece.c_str())) {
			break;
		}
		for (int y = 0; ; y++) {
			std::ostringstream y_folder;
			y_folder << std::setw(3) << std::setfill('0') << y;

			std::string fullPath = baseFolder + x_folder.str() + y_folder.str() + "\\attr.atr";
			if (fileExists(fullPath.c_str())) {
				CPlayer& player = CPlayer::Instance();
				EterFile* f = player.CGetEter(fullPath.c_str());
				MapPiece *p = new MapPiece(f, x, y, baseFolder + x_folder.str() + y_folder.str());
				buffer.push_back(p);
				//p->printToFile((x_folder.str() + y_folder.str()).c_str());

			}
			else {
				if (y > largestYPiece) {
					largestYPiece = y;
				}
				break;
			}
		}
	}
	if (buffer.size() == 0 || xPieces==0) {
		LOG_ERROR("No map found with name %s", mapName.c_str());
		return false;
	}
	else {
		LOG_DEBUG("Map %s loaded max_X_Piece:%d max_Y_Piece:%d", mapName.c_str(), xPieces,largestYPiece);
	}



	maxY = largestYPiece *ATTR_HEIGHT;
	maxX = xPieces * ATTR_WIDTH;

	int allocSpace = maxX *maxY;
	map = (BYTE*)malloc(allocSpace);
	memset(map, 0, allocSpace);

	for (auto mapPiece : buffer) {
		if (!addMapPiece(mapPiece)) {
			LOG_ERROR("MapPiece exceeds limits %s", mapName.c_str());
			return false;
		}
		for (auto & object : mapPiece->area.vec) {
			objects.push_back(object);
		}

		delete mapPiece;
	}

	for (int i = 0; i< allocSpace; i++) {
		if (map[i] & 0x1) {
			map[i] = 0;
		}else{ map[i] = 1; }
	}


	increaseBlockedArea();
	return true;

}

bool MapCollision::addMapPiece(MapPiece * piece)
{
	if (piece->dataSize > ATTR_HEIGHT*ATTR_WIDTH) {
		return false;
	}
	for (int y = 0; y < ATTR_HEIGHT; y++) {
		void* startLoc = (void*)((int)map + (piece->yStart + y)*maxX + piece->xStart);
		void* pieceStart = (void*)((int)piece->getMapData() + y * ATTR_WIDTH);
		memmove(startLoc, pieceStart, ATTR_WIDTH);
	}

	return true;
}

void MapCollision::printToFile(const char* name)
{
	std::ofstream myfile;
	myfile.open(name);
	char* line = (char*)malloc(maxX);
	for (int y = 0; y < maxY; y++) {
		for (int x = 0; x < maxX; x++) {
			line[x] = 48 + isBlocked(x, y);
		}
		myfile << line;
		myfile << "\n";
	}
	free(line);
	myfile.close();
	
}

void MapCollision::addObjectsCollisions()
{
	for (auto obj : objects) {
		setByte(0,obj.x / 100, abs(obj.y / 100));
	}
}

void MapCollision::increaseBlockedArea()
{
	std::list<Point> bufferPoints;
	for (int y = 0; y < maxY; y++) {
		for (int x = 0; x < maxX; x++) {
			if (!isBlocked(x,y)) {
				if (isBlocked(x-1, y-1)) {
					bufferPoints.push_back(Point(x, y));
					continue;
				}
				if (isBlocked(x-1, y)) {
					bufferPoints.push_back(Point(x, y));
					continue;
				}
				if (isBlocked(x-1, y+1)) {
					bufferPoints.push_back(Point(x, y));
					continue;
				}
				if (isBlocked(x + 1, y-1)) {
					bufferPoints.push_back(Point(x, y));
					continue;
				}
				if (isBlocked(x + 1, y)) {
					bufferPoints.push_back(Point(x, y));
					continue;
				}
				if (isBlocked(x + 1, y+1)) {
					bufferPoints.push_back(Point(x, y));
					continue;
				}
				if (isBlocked(x, y+1)) {
					bufferPoints.push_back(Point(x, y));
					continue;
				}
				if (isBlocked(x, y-1)) {
					bufferPoints.push_back(Point(x, y));
					continue;
				}
			}
		}
	}
	 
	for (Point &a : bufferPoints) {
		setByte(0, a.x, a.y);
	}




}

bool MapCollision::loadMapFromDisk()
{
	std::string dic(getMapsPath());
	dic += mapName + std::string(".dat");

	std::ifstream fin(dic.c_str());

	if (fin.fail())
	{
		return false;
	}
	std::string line;
	std::getline(fin, line);
	std::stringstream sl(line);
	
	sl >> maxX >> maxY;
	if (maxX == 0 || maxY == 0) {
		return false;
	}
	if (map) {
		free(map);
	}
	map = (BYTE*)calloc(maxX * maxY,1);
	int y = 0;
	while (std::getline(fin, line)) {
		for (int x = 0; x < maxX && x < line.size();x++) {
			if (line.at(x) == '1') {
				map[maxX * y + x] = 1;
			}
		}
		y++;
	}
	LOG_INFO("Map %s in disk loaded from %s with X:%d ,Y:%d loaded with success!", mapName.c_str(), dic.c_str(), maxX, maxY);
	fin.close();
	return true;

}

void MapCollision::saveMap()
{
	std::string dic(getMapsPath());

	// Not system("mkdir ..."): shelling out from the game thread flashes a cmd.exe window on every
	// save. SHCreateDirectoryExA creates the intermediate dirs, but it wants a fully-qualified path
	// with no trailing separator (some Windows versions answer ERROR_BAD_PATHNAME to one) and
	// getMapsPath() ends in one, hence the strip -- and it reports an already-existing dir as an
	// error code rather than success, hence the two ignored codes.
	std::string dir(dic);
	while (!dir.empty() && (dir[dir.size() - 1] == '\\' || dir[dir.size() - 1] == '/'))
		dir.erase(dir.size() - 1);

	int rc = SHCreateDirectoryExA(NULL, dir.c_str(), NULL);
	if (rc != ERROR_SUCCESS && rc != ERROR_ALREADY_EXISTS && rc != ERROR_FILE_EXISTS) {
		LOG_ERROR("Could not create map cache dir %s (code %d)", dir.c_str(), rc);
		return;
	}

	dic += mapName + std::string(".dat");

	std::ofstream myfile;
	myfile.open(dic.c_str());
	if (myfile.fail()) {
		LOG_ERROR("Couldn't save current map to %s", dic.c_str());
		return;
	}

	myfile << maxX << " " << maxY << "\n";
	char* line = (char*)calloc(maxX+2,1);
	for (int y = 0; y < maxY; y++) {
		int x = 0;
		for (x = 0; x < maxX; x++) {
			line[x] = 48 + !isBlocked(x, y);
		}
		line[x] = '\n';
		myfile << line;
	}
	LOG_INFO("Current map saved to %s", dic.c_str());
	free(line);
	myfile.close();
}


MapCollision::MapPiece::MapPiece(EterFile * file,int x,int y,std::string path)
{
	size = file->size;
	entireMap = (BYTE*)malloc(size);
	memcpy(entireMap, file->data, size);
	WORD* buf = (WORD*)entireMap;
	version = buf[0];
	width = buf[1];
	height = buf[2];
	dataSize = size - HEADER_SIZE;
	xStart = x*ATTR_WIDTH;
	yStart = y*ATTR_HEIGHT;

	std::string areaData = path + "\\areadata.txt";
	CPlayer& player = CPlayer::Instance();
	auto eterArea = player.CGetEter(areaData.c_str());
	area.loadFile((char*)eterArea->data,eterArea->size);

	//printf("version %d width= %d height =%d size=%d x=%d y=%d\n", version, width, height, size, xStart, yStart);

}



MapCollision::MapPiece::~MapPiece()
{
	free(entireMap);
}

BYTE* MapCollision::MapPiece::getMapData()
{
	return (BYTE*)((int)entireMap + HEADER_SIZE);
}

void MapCollision::MapPiece::printToFile(const char * name)
{
	std::ofstream myfile;
	myfile.open(name);
	BYTE* data = getMapData();
	char* line = (char*)calloc(ATTR_WIDTH,1);
	for (int y = 0; y < ATTR_HEIGHT; y++) {
		for (int x = 0; x < ATTR_WIDTH; x++) {
			line[x] = 48 + (data[y*ATTR_WIDTH + x] & 0x1);
		}
		myfile << line;
		myfile << "\n";
	}
	free(line);
	myfile << "X_Start: " << std::to_string(xStart) << "Y_Start: " << std::to_string(yStart);
	myfile.close();
}

void AreaData::loadFile(char * buffer, int size)
{
	std::stringstream membuf(std::ios::in | std::ios::out | std::ios::binary);
	membuf.write(buffer, size);
	membuf.seekg(std::ios::beg);
	std::string line;
	std::string startObjectsPrefix = "Start Object";
	while (std::getline(membuf, line)) {
		if (line.find(startObjectsPrefix) != std::string::npos) {
			Object obj;
			//Coords
			std::getline(membuf, line);
			const char* coordsText = line.c_str();
			sscanf(coordsText, "%f %f %f", &obj.x, &obj.y, &obj.z);

			//crc
			std::getline(membuf, line);
			/*coordsText = line.c_str();
			sscanf(coordsText, "%d", &obj.crc);*/

			//Rotation
			std::getline(membuf, line);
			coordsText = line.c_str();
			sscanf(coordsText, "%f#%f#%f", &obj.rotX, &obj.rotY, &obj.rotZ);

			//Uknown
			std::getline(membuf, line);
			coordsText = line.c_str();
			sscanf(coordsText, "%d", &obj.uknown);

			vec.push_back(obj);
		}

	}
}
