// What differs between Renku/Haiku and Linux (Cosmoe). Platform.cpp only.
#pragma once

#include "Core.h"

std::vector<Drive> ListDrives();
// Unmounts the drive's volumes before it is overwritten; false + *error if not.
bool PrepareDrive(const Drive& drive, std::string* error);
// A command that copies `image` onto the drive and prints GNU dd progress.
std::vector<std::string> WriteCommand(const std::string& image,
	const Drive& drive);
std::vector<std::string> RestartCommand();
