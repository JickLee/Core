/*
    Copyright (C) 2009 Johannes Schindelin (johannes.schindelin@gmx.de)

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version
    3 of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "ppm.h"

extern "C" {
static int dumper_thread(void *arg) {
    FrameExporter *e = static_cast<FrameExporter *>(arg);

    e->dumpThr();

    return 0;
}
};

// FrameExporter

FrameExporter::FrameExporter() {

    //this now assumes the display is setup
    //before the frame exporter is created
    //(which seems reasonable)

    rowstride     = display.width * 3;

    pixels1        = new char[display.height * rowstride];
    pixels2        = new char[display.height * rowstride];
    pixels_out     = new char[display.height * rowstride];

    pixels_shared_ptr = 0;

    dumper_thread_state = FRAME_EXPORTER_WAIT;

    cond   = SDL_CreateCond();
    mutex  = SDL_CreateMutex();

#if SDL_VERSION_ATLEAST(1,3,0)
    thread = SDL_CreateThread( dumper_thread, "frame_exporter", this );
#else
    thread = SDL_CreateThread( dumper_thread, this );
#endif
}

FrameExporter::~FrameExporter() {
    stopDumpThr();

    SDL_KillThread(thread);
    SDL_DestroyCond(cond);
    SDL_DestroyMutex(mutex);

    pixels_shared_ptr = 0;

    delete[] pixels1;
    delete[] pixels2;
    delete[] pixels_out;
}

void FrameExporter::stopDumpThr() {
    if(dumper_thread_state == FRAME_EXPORTER_STOPPED) return;

    SDL_mutexP(mutex);

        dumper_thread_state = FRAME_EXPORTER_EXIT;

        SDL_CondSignal(cond);

    SDL_mutexV(mutex);

    //busy wait for thread to exit
    while(dumper_thread_state != FRAME_EXPORTER_STOPPED)
        SDL_Delay(100);
}

void FrameExporter::dump() {

    display.mode2D();

    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);

    char* next_pixel_ptr = (pixels_shared_ptr == pixels1) ? pixels2 : pixels1;

    // copy pixels - now the right way up
    glReadPixels(0, 0, display.width, display.height,
        GL_RGB, GL_UNSIGNED_BYTE, next_pixel_ptr);

    // wait for lock before changing the pointer to point to our new buffer
    SDL_mutexP(mutex);

        //flip buffer we are pointing at
        pixels_shared_ptr = next_pixel_ptr;
        dumper_thread_state = FRAME_EXPORTER_DUMP;

    SDL_CondSignal(cond);
    SDL_mutexV(mutex);
}

void FrameExporter::dumpThr() {

    SDL_mutexP(mutex);

    while(dumper_thread_state != FRAME_EXPORTER_EXIT) {

        dumper_thread_state = FRAME_EXPORTER_WAIT;

        while (dumper_thread_state == FRAME_EXPORTER_WAIT) {
            SDL_CondWait(cond, mutex);
        }

        if (dumper_thread_state == FRAME_EXPORTER_EXIT) break;

        if (pixels_shared_ptr != 0) {

            //invert image
            for(int y=0;y<display.height;y++) {
                for(int x=0;x<rowstride;x++) {
                    pixels_out[x + y * rowstride] = pixels_shared_ptr[x + (display.height - y - 1) * rowstride];
                }
            }

            dumpImpl();
        }
    }

    dumper_thread_state = FRAME_EXPORTER_STOPPED;

    SDL_mutexV(mutex);

}

// PPMExporter

PPMExporter::PPMExporter(std::string outputfile) {

    if(outputfile == "-") {
        output = &std::cout;

    } else {
        filename = outputfile;
        output   = new std::ofstream(outputfile.c_str(), std::ios::out | std::ios::binary);

        if(output->fail()) {
            delete output;
            throw PPMExporterException(outputfile);
        }
    }

    //write header
    sprintf(ppmheader, "P6\n# Generated by %s\n%d %d\n255\n",
        gSDLAppTitle.c_str(), display.width, display.height
    );
}

PPMExporter::~PPMExporter() {
    stopDumpThr();

    if(filename.size()>0)
        ((std::fstream*)output)->close();
}

void PPMExporter::dumpImpl() {
    *output << ppmheader;
    output->write(pixels_out, rowstride * display.height);
}
