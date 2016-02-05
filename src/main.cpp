/* 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This program is influenced by Vangelis's perl script found at 
 * https://github.com/cyberang3l/timelapse-deflicker. I have used the same
 * logic present in the script. The additional thing implemented here is mainly 
 * on the performance side. 
 */
#include <Magick++.h>
#include <magick/statistics.h>
#include <string>
#include <iostream>
#include <vector>
#include <string.h>
#include <dirent.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/stat.h>
#include <stdint.h>

//#include "tinyxml2.h"

pthread_mutex_t vector_mutex ;

using namespace std;
using namespace Magick;

char workingDirectory[1024] = "./"; // default is pwd
char outputDirectory[1024]  = "Deflickered";
char cropDimension[24];
bool mCropImage = false ;
int numThreads = 2; // max thread ccan be equal to # of CPU cores
int imageCount = 0; 
int debugLevel = 1; // can be between 1 to 4
int rollingWindow = 15;
bool processFaster = false;
char *version = "1.0";

pthread_t luminance_thread[4];
pthread_t image_write_thread[4];

// Structure to hold each image's information
struct _image_info {
	char fileName[256];
	char absFileName[1024+256];
	double originalLuminance;
	double newLuminance;
	double brightnessToChange;
};

typedef struct Thread_data { 
	int start;
	int end;
}THREAD_DATA;

typedef struct _image_info image_info;

// Container to hold list of all images
std::vector<image_info*> imageList;

//char *xmlString = "<x:xmpmeta xmlns:x='adobe:ns:meta/' x:xmptk='Image::ExifTool 9.46'>\n<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>\n\t<rdf:Description rdf:about=''\n\t\txmlns:luminance='https://github.com/cyberang3l/timelapse-deflicker'>\n\t\t<luminance:luminance>0.344221192496706</luminance:luminance>\n\n</rdf:Description>\n</rdf:RDF>\n</x:xmpmeta>";

/* printUsage() 
 * Function to print program's usage information
 */
void printUsage(char *binaryName, int verbose)
{
	fprintf(stdout, "Usage : %s [ -d <path/to/image/directory/>] [-t <number>] [-v| --version] [ -h|--help]", binaryName);

	if ( verbose == 2 ) 
	{
		fprintf(stdout, " [--fast] [--crop <dimensions> ]");
	}
	fprintf (stdout, "\n");

	if ( verbose >= 1 )
	{
		fprintf(stdout, "Where : \n");
		fprintf(stdout, "  -d\t\tPath to the directory where images are located. Default is pwd\n");
		fprintf(stdout, "  -t\t\tNumber threads to create for processing. Default is 2, max is 4\n");

		if ( verbose == 1 ) 
			fprintf ( stdout, "\nUse --help to get more advanced options..\n");	

		if ( verbose == 2 ) 
		{
			fprintf(stdout, "--fast\t\tMake luminance calculation faster compromising on the data accuracy\n");		
			fprintf(stdout, "--crop\t\tCrop the final image after applying brightness\n");
			fprintf(stdout, "\t\tExamples : \n");
			fprintf(stdout, "\t\t  --crop \"hd\" : crops the image to 1920x1080 \n");
			fprintf(stdout, "\t\t  --crop \"2k\" : crops the image to 2048x1152 \n");
			fprintf(stdout, "\t\t  --crop \"4k\" : crops the image to 3840x2160 \n");
			fprintf(stdout, "\t\t  --crop \"800x600!\" : crops the image to specified dimension\n");
		}
		fprintf(stdout, "\n");
	}
	exit(1);
}

int getCPUCount()
{
	cpu_set_t cs;
	CPU_ZERO(&cs);
	sched_getaffinity(0, sizeof(cs), &cs);

	int count = 0;
	for (int i = 0; i < 8; i++)
	{
		if (CPU_ISSET(i, &cs))
			count++;
	}
	return count;
}

/* this is not complete and not used for now */
/*
void progress_bar()
{
	float progress = 0.0;
	while (progress < 1.0) {
    int barWidth = 70;

    std::cout << "[";
    int pos = barWidth * progress;
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << int(progress * 100.0) << " %\r";
    std::cout.flush();

    progress += 0.16; // for demonstration only
	}
	std::cout << std::endl;
}
*/

void processCmdLine(int argc, char **argv)
{
	int param = 0;
	int i = 0;
	int cpuCount = 0;

	if ( argc < 2 ) 
	{
		printUsage(argv[0], 0);
	}

	for(i = 1; i < argc; i++ )
	{
		if( '-' == *argv[i] )
		{
			if ( !strcmp("-d", argv[i]))
				strcpy(workingDirectory,  argv[++i]);
			else if ( !strcmp("-t", argv[i])){
				numThreads = atoi(argv[++i]);
				cpuCount = getCPUCount();
				if ( numThreads > cpuCount )
				{
					std::cout << "WARNING : System has only " << cpuCount << " cores. Setting number of threads to " << cpuCount <<endl; 
					numThreads = cpuCount;
				}
			}
			else if ( !strcmp("--debug-level", argv[i]))
				debugLevel = atoi(argv[++i]);
			else if ( !strcmp("--fast", argv[i]))
				processFaster = true;
			else if ( !strcmp("--crop", argv[i])){
				strcpy(cropDimension, argv[++i]);
				mCropImage = true;
			}
			else if ( !strcmp("-h", argv[i])) 
				printUsage(argv[0], 1);
			else if ( !strcmp("--help", argv[i])) 
				printUsage(argv[0], 2);
			else if ( !strcmp("--version", argv[i]) || !strcmp("-v", argv[i])) {
				std::cout << "timelapse-deflicker Version : " << version << endl ;
				exit(1);
			}
		}
		else 
		{
			fprintf(stderr, "Unrecognized parameter : %s\n", argv[i]);
			printUsage(argv[0], 0);
		}
	}
}

/* We currently support only JPEG files
 * JPEG file header has 0xff 0xd8 as the first 2 bytes
 * we check for the same here 
 */
bool is_image_file(char *file)
{
	FILE *fd;
	char buf[4];
	uint8_t b1, b2;
	int n;

	fd = fopen(file, "rb");

	if (fd == NULL ) 
		return false;

	n = fread(buf, 1, 4, fd);

	b1 = buf[0];
	b2 = buf[1];

	fclose(fd);

	if ( b1 == 0xff && b2 == 0xd8 ) 
		return true;
	else 
		return false;
}

/* read_files ()
 * Function to read each image file. Creates
 * image_info strucuture for each images and 
 * then appends it to imageList vector 
 */
void read_files()
{
	image_info *img;
	DIR *dir;
	struct dirent *ent;
	struct dirent **namelist;
	int numEntries;

	numEntries = scandir(workingDirectory, &namelist, NULL, alphasort);
	for(int i=0; i< numEntries; i++) {

		if ( !strcmp(".", namelist[i]->d_name) || !strcmp("..", namelist[i]->d_name))
			continue;

		if ( debugLevel > 3 ) 
			cout << "INFO : Reading file -" <<  namelist[i]->d_name << endl;

		if ( strlen(namelist[i]->d_name) > 126 )
		{
				fprintf(stdout, "WARNING :  %s file name is longer than 126 characters.. Ignoring\n", namelist[i]->d_name);
		}	
		else {  
			img = new image_info();
			strcpy(img->absFileName, workingDirectory);
			strcat(img->absFileName, "/");
			strcat(img->absFileName, namelist[i]->d_name);
			strcpy(img->fileName, namelist[i]->d_name);

			img->originalLuminance = 0;
			img->newLuminance = 0;
			img->brightnessToChange = 0;
	
			if ( ! is_image_file(img->absFileName) ){
				if ( debugLevel > 2 ) 
					cout << "INFO : File  " <<  namelist[i]->d_name << " does not seem to be image file, Skipping..." << endl;
						
				continue;
			}
			imageList.push_back(img);
			imageCount ++;

		}
		free(namelist[i]);
	}
}

/* get_image_luminance()
 * Function to calculate luminance value of
 * a given image. The math to calculate is taken 
 * directly from Vangelis perl script
 */
double get_image_luminance(char *image_file)
{
	Magick::ImageStatistics statistics;
	double red, green, blue;
	double origValue;
	std::vector<image_info*>::const_iterator i;

	Image my_image;
	
	try { 
		my_image.read(image_file);
		/* resizing the image will get the statistics faster */
		if ( processFaster ) 
			my_image.scale( Geometry(800, 600));
	}
	catch ( Exception &error_ )
	{
		/* We might have read a non image file to come here */
		if ( debugLevel > 3 ) 
			cout << "WARNING : " <<  image_file << " seems to be non image file, Skipping.. " ;
		if ( debugLevel > 4 ) 
			cout << error_.what() ;
		cout << endl;
		return -1;
	}

	/* Calculate image statistics */
	my_image.statistics(&statistics);

	/* read the R,G,B's mean values and 
	 * calculate the luminance 
	 */
	red   = statistics.red.mean;
	green = statistics.green.mean;
	blue  = statistics.blue.mean;

	origValue = ( red * 0.299 ) + (  green * 0.587)  + (blue * 0.114);
	return origValue;
}

/* _calculate_luminance()
 * Function which calculate luminance of
 * a range of images from imageList in 
 * a separate thread 
 */
void* _calculate_luminance( void *T)
{
	int start, end;
	int rc;
	double luminance;

	THREAD_DATA *data = (THREAD_DATA*) T;

	start = data->start;
	end	  = data->end;

	for( int i=start;i< end; i++)
	{
		luminance = get_image_luminance(imageList[i]->absFileName);

		if ( luminance == -1 ){
			imageList.erase(imageList.begin() + i);
			continue;
		}
		if ( debugLevel > 2 ) 
			std::cout << "DEBUG : Luminance of image " << imageList[i]->fileName << ": " <<  luminance << "\n" ;

		/* set both original & new valu to calculated values */
		imageList[i]->originalLuminance = luminance;
		imageList[i]->newLuminance = luminance;
	}
}

/* calculate_luminance()
 * Top level luminance calculate function which will 
 * distributes the images between the threads created. 
 * Uses CPU affinity to make each thread on different 
 * CPU cores to boost performance 
 */
void calculate_luminance()
{
	THREAD_DATA *data[4];
	cpu_set_t cpuset;
	int mStart = 0;
	int rc;
	void *status;
	int incr = imageCount/numThreads;

	if ( debugLevel )
		cout << "INFO : " <<  "Calculating origianl luminance... " << endl;

	for (int i=0;i< numThreads; i++)
	{
		data[i] = new THREAD_DATA();
		data[i]->start = mStart ;

		/* For last thread, make sure you give all images */
		if ( i == ( numThreads -1 ))
		{
				data[i]->end = imageCount;
		}
		else 
			data[i]->end = ( mStart + incr );

		if ( debugLevel > 3 ) 
			cout <<  "DEBUG : Creating Thread " << i << "..." << endl;

		pthread_create(&luminance_thread[i], NULL, _calculate_luminance, data[i]);
		CPU_ZERO(&cpuset);
		CPU_SET(i, &cpuset);

		mStart = mStart + incr ;

		rc = pthread_setaffinity_np(luminance_thread[i], sizeof(cpu_set_t), &cpuset);

		if ( debugLevel > 3 ) 
		{
			rc = pthread_getaffinity_np(luminance_thread[i], sizeof(cpu_set_t), &cpuset);			
			printf("Set returned by pthread_getaffinity_np function :");
			for (int j = 0; j < CPU_SETSIZE; j++)
				 if (CPU_ISSET(j, &cpuset))
						printf("    CPU %d\t", j);
			printf("\n");
		}

	}

	/* Wait for all the threads to finish */
	for (int i=0;i<numThreads;i++)
		rc = pthread_join(luminance_thread[i], &status);

	for( int i=0;i<numThreads;i++)
	{
		if(data[i])
			free(data[i]);
	}
}


/* calculate_new_luminance()
 * Function to calculate new luminance values for 
 * images.This function is direct port of logic
 * from perl script. 
 */
void calculate_new_luminance()
{
	int lowWindow = rollingWindow/2;
	int highWindow = rollingWindow - lowWindow;
	int mCount;
	double avgLuminance = 0;

	if ( debugLevel ) 
		cout <<  "INFO : Calculating new luminance values..." << endl;

	for (int i=0;i < imageCount; i++ )
	{
		mCount = 0 ;
		avgLuminance = 0;

		for (int j= ( i- lowWindow ); j < ( i + highWindow );j++)
		{
			if ( j >= 0 && j < imageCount ){ 
				mCount ++;
				avgLuminance += imageList[j]->newLuminance;
			}
		}
		//cout << "count " << mCount << "Average Lumminance for " << imageList[i]->fileName << ": " <<  avgLuminance << endl;
		
		imageList[i]->newLuminance = avgLuminance/mCount;
	}
}

void *_write_new_images(void *T)
{
	int start, end;
	int rc;
	Image new_image;
	double newBrightness;
	char mOutFilename[1024+256];
	char dimension[24];

	THREAD_DATA *data = (THREAD_DATA*) T;

	start = data->start;
	end   = data->end;

	for (int i=start ; i< end; i++)
	{
		newBrightness = ( 1 / (imageList[i]->originalLuminance/imageList[i]->newLuminance) * 100 ) ;	

		strcpy(mOutFilename, workingDirectory);
		strcat(mOutFilename, "/");
		strcat(mOutFilename, outputDirectory);
		strcat(mOutFilename, "/");
		strcat(mOutFilename, imageList[i]->fileName);

		try { 
			new_image.read(imageList[i]->absFileName);
			new_image.modulate(newBrightness, 100, 100);
			new_image.quality(95);

			if ( mCropImage ) 
			{
				if ( !strcmp(cropDimension, "hd") )
					strcpy(dimension, "1920x1080!");
				else if ( !strcmp(cropDimension, "2k") )
					strcpy(dimension, "2048x1152!");
				else if ( !strcmp(cropDimension, "4k") )
					strcpy(dimension, "3840x2160!");
				else 
					strcpy(dimension, cropDimension);

				if ( debugLevel > 2 ) 
					cout << "Cropping image to dimension : " << dimension << endl; 
				new_image.scale( Geometry(dimension));
			}
			new_image.write(mOutFilename);
		}
		catch ( Exception &error_ )
		{
			/* We might have read a non image file to come here */
			if ( debugLevel > 3 ) 
				cout << "WARNING : " <<  imageList[i]->fileName << " non image file, Skipped.. " ;
			if ( debugLevel > 4 ) 
					cout << error_.what() ;
			cout << endl;
		}
		if ( debugLevel > 2 ) 
			std::cout << "DEBUG : Setting the brightness of the image " << imageList[i]->fileName << " : " << newBrightness << endl;

	}// end of for
}
/* generate_images_with_new_luminance()
 * Function to create new images with the calculated 
 * luminance values. It will set the brightness of the image 
 * based on the new luminance value
 */
void generate_images_with_new_luminance()
{
	struct stat d;
	char mOut[1024];
	THREAD_DATA *data[4];
	int rc;
	int mStart = 0;;
	cpu_set_t cpuset;
	int incr = imageCount/numThreads;
	void *status;

	/* first create the output directory if not already present */
	strcpy(mOut, workingDirectory);
	strcat(mOut, "/");
	strcat(mOut, outputDirectory);

	/* If output directory is not present, create one */
	rc = stat(mOut, &d);
	
	if ( rc == -1 ) 
	{
		if (debugLevel > 3 )
			cout << "DEBUG : Creating output directory - " <<  mOut << endl;

		mkdir(mOut, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	}
	else if ( ! S_ISDIR(d.st_mode) )
	{
		cout << "WARNING : " << mOut << " is not a directory.. exiting " << endl;
		exit (1);
	}

	if (debugLevel > 3 )
		cout <<  "DEBUG : Output directory - " << mOut << " already present " << endl;

	if ( debugLevel ) 
		cout <<  "INFO : Writing images with new brighness value..." << endl;

	for (int i=0;i< numThreads; i++)
	{
		data[i] = new THREAD_DATA();
		data[i]->start = mStart ;

		/* For last thread, make sure you give all images */
		if ( i == ( numThreads -1 ))
		{
				data[i]->end = imageCount;
		}
		else 
			data[i]->end = ( mStart + incr );

		if ( debugLevel > 3 ) 
			cout <<  "DEBUG : Creating Thread " << i << " to write new images.." << endl;

		pthread_create(&image_write_thread[i], NULL, _write_new_images, data[i]);

		CPU_ZERO(&cpuset);
		CPU_SET(i, &cpuset);

		mStart = mStart + incr ;

		rc = pthread_setaffinity_np(image_write_thread[i], sizeof(cpu_set_t), &cpuset);

	}
	/* Wait for all the threads to finish */
	for (int i=0;i<numThreads;i++)
		rc = pthread_join(image_write_thread[i], &status);

}


int main(int argc, char *argv[])
{
	Image tmp;
	double brightness;
	std::vector<image_info*>::const_iterator i;

	InitializeMagick(*argv);

	// Process command line 
	processCmdLine(argc, argv);

	read_files();

	calculate_luminance();


	/* Now start calculating new luminance for images */
	calculate_new_luminance();

	if ( debugLevel > 1 ) 
	{
		std::cout << "DEBUG : Printing Luminance value..." << endl;

		for(i = imageList.begin(); i!= imageList.end(); ++i)
		{
			std::cout << (*i)->fileName << ": old = " << (*i)->originalLuminance << " New = " << (*i)->newLuminance << endl;
		}
	}
	generate_images_with_new_luminance();	

	cout << "INFO : Finished processing " << imageCount << " images.." << endl;
	
	return 0;
}


