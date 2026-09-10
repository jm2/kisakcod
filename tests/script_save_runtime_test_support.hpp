#pragma once
// External-service doubles for the production save-flow slices.
scr_classStruct_t g_classMap[CLASS_NUM_COUNT] = {
    {100, 10, 'e', "entity"}, {101, 20, 'h', "hudelem"},
    {102, 30, 'p', "pathnode"}, {103, 40, 'v', "vehiclenode"}
};
std::vector<unsigned int> removedSaveIds, writtenSaveIds, addedSaveIds;
std::vector<const char *> leakPositions;
unsigned char nextClassTag = 61;
int debuggerRestoreCount = 0;
void RemoveRefToObject(uint32_t id) { removedSaveIds.push_back(id); }
void WriteId(unsigned int id, unsigned int, MemoryFile *) { writtenSaveIds.push_back(id); }
void AddSaveObject(unsigned int id) { addedSaveIds.push_back(id); }
unsigned int Scr_ReadId(MemoryFile *, unsigned int tag) { return tag; }
void MemFile_ReadData(MemoryFile *, int bytes, unsigned char *out) {
    Check(bytes == 1);
    *out = nextClassTag++;
}
static void Scr_AddDebuggerRefs() { ++debuggerRestoreCount; }
void QDECL Com_Printf(int, const char *, ...) {}
void Scr_PrintPrevCodePos(int, char *position, uint32_t) { leakPositions.push_back(position); }
