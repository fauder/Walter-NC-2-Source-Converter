// Corrade Includes.
#include <Corrade/Containers/Optional.h>
#include <Corrade/Containers/StaticArray.h>
#include <Corrade/Utility/Directory.h>
#include <Corrade/Utility/String.h>

// std Includes.
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace Dir = Corrade::Utility::Directory;
namespace Str = Corrade::Utility::String;

Corrade::Containers::Optional< std::string > ParseAndProcessFile( const std::string& filePath, const std::string& outputDirectoryPath )
{
	std::stringstream outputStream;
	outputStream << "buffer.clear();" << "\n\n// Initial Setup.\n";

	std::stringstream fileStream( Dir::readString( filePath ) );
	std::string line;
	int currentPassNumber = 1;
	std::streampos lastLineStartPos = 0;
	while( std::getline( fileStream, line ) )
	{
		Str::ltrimInPlace( line );

		// Skip comments.
		if( line.empty() || line.front() == ';' || line.find( "M6" ) != std::string::npos )
			continue;

		// Strip comments from the back end of the line.
		if( const auto result = Str::partition( line, ";" ); !result.back().empty() )
			line = Str::rtrim( result.front() );

		lastLineStartPos = outputStream.tellp();
		outputStream << R"(format_to( buffer, ")";

		std::stringstream lineStream( line );

		std::vector< std::string > arguments;

		std::string nCode;
		lineStream >> nCode;

		bool has_N_Code = !nCode.empty() && nCode.front() == 'N';
		if( has_N_Code )
			outputStream << "N{:d}"; // No explicit argument name for linenumber as it is kind of obvious.

		bool createNewPassSection = false;

		// Don't use G43 itself. This line initially positions the tool 2.0 mm above the workpiece.
		if( line.find( "G43" ) != std::string::npos )
		{
			outputStream << R"( G01 X{x:.3f} Y{y:.3f} Z{z:.3f} F1000)";
			arguments.push_back( R"(arg( "x", cutterPosition.x() ))" );
			arguments.push_back( R"(arg( "y", cutterPosition.y() ))" );
			arguments.push_back( R"(arg( "z", cutterPosition.z() ))" );
		}
		else if( const auto result = Str::rpartition( line, "G41 D.." ); !result.front().empty() )
		{
			auto restOfLine = result.back();
			while( restOfLine.back() == '\r' ) // While loop because there may be empty lines between actual G code lines...
				restOfLine.pop_back();
			outputStream << R"( G41 D{toolNo:d})" << restOfLine;
			arguments.push_back( R"(arg( "toolNo", toolNo ))" );
		}
		else
		{
			std::getline( lineStream, line ); // Read the REST of the line into line.
			while( line.back() == '\r' ) // While loop because there may be empty lines between actual G code lines...
				line.pop_back();
			outputStream << line;

			createNewPassSection = line.find( "G91" ) != std::string::npos || line.find( "G40" ) != std::string::npos;
		}

		outputStream << R"(\n")";

		if( has_N_Code || !arguments.empty() )
		{
			outputStream << ", ";

			if( has_N_Code )
				outputStream << "lineNumber++";

			if( !arguments.empty() )
			{
				outputStream << ",\n           ";
				for( auto& argument : arguments )
					outputStream << argument << ", ";
				outputStream.seekp( ( long )outputStream.tellp() - 2 );
			}
		}

		outputStream << " );\n";

		if( createNewPassSection )
			outputStream << "\n" << "// Pass #" << currentPassNumber++ << ".\n";
	}

	outputStream.seekp( ( long )lastLineStartPos - 12 /* 12 is the length of last "Pass ##." string.*/ );

	outputStream << "// Return tool to initial position.\n";
	outputStream << R"(format_to( buffer, "N{:d} G00 G90 X{x:.3f} Y{y:.3f} Z{z:.3f}\n", lineNumber++,)" << "\n";
	outputStream << R"(           arg( "x", cutterPosition.x() ), arg( "y", cutterPosition.y() ), arg( "z", cutterPosition.z() ) );)";

	outputStream << "\n\n" << "return std::string_view( buffer.data(), buffer.size() );";

	if( !Dir::isDirectory( outputDirectoryPath ) && !Dir::mkpath( outputDirectoryPath ) )
		return Corrade::Containers::NullOpt;

	std::string outputFileName( outputDirectoryPath + Dir::splitExtension( Dir::filename( Dir::fromNativeSeparators( filePath ) ) ).first + "_WalterSource.txt" );
	if( std::ofstream outputFile( outputFileName ); outputFile )
	{
		outputFile << outputStream.str();
		return outputFileName;
	}

	return Corrade::Containers::NullOpt;
}

int main( int argc, char** argv )
{
	std::string directory;
	if( argc == 1 )
		directory = Dir::current() + '/';
	else if( argc >= 2 )
		directory = Dir::fromNativeSeparators( argv[ 1 ] );

	int numberOfProcessed = 0;
	for( const auto& fileName : Dir::list( directory, Dir::Flag::SkipDirectories ) )
	{
		std::cout << R"(File ")" << fileName << R"(" found in directory.)" << "\n";
		const auto filePath = directory + fileName;
		if( auto [ filePathWithoutExtension, extension ] = Dir::splitExtension( filePath ); extension == ".txt" )
		{
			if( auto result = ParseAndProcessFile( filePath, directory + "Source/" ); result )
			{
				std::cout << R"(File ")" << Dir::toNativeSeparators( *result ) << R"(" is generated.)" << "\n";
				numberOfProcessed++;
			}
		}
	}

	if( numberOfProcessed == 0 )
		std::cerr << R"(Directory ")" << directory << R"(" does not contain any files with .txt extension.)" << "\n";

	return 0;
}