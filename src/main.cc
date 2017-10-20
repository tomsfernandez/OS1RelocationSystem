/**
* This file is part of ORB-SLAM2.
*
* Copyright (C) 2014-2016 Raúl Mur-Artal <raulmur at unizar dot es> (University of Zaragoza)
* For more information see <https://github.com/raulmur/ORB_SLAM2>
*
* ORB-SLAM2 is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* ORB-SLAM2 is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with ORB-SLAM2. If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file main
 * main prepara el "sensor" de imágenes monocular e inicia el bucle principal, enviando una imagen Mat por vez.
 * Previamente inicializa el sistema al construir el objeto SLAM, pasándole como argumentos las rutas al vocabulario y configuración,
 * y el modo MONOCULAR.
 */


#include <iostream>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <unistd.h>
#include <thread>
#include <string>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include <System.h>
#include <Viewer.h>
#include <Tracking.h>
#include <FrameDrawer.h>
#include <Serializer.h>
#include <Video.h>
#include <include/TcpSocketImageDecoder.h>

using namespace std;

ORB_SLAM2::System *Sistema;

/**
 * Cambios hechos:
 *
 * - Agregué el código para que apretando la tecla m, imprima la matriz de rototraslación del
 * frame actual solo si está inicializado ORB_SLAM.
 *
 * - Arreglé el error de compilación de la SparseMatrix de Eigen, cambiando el SparseMatrix::Index por un
 * int en el field que usa Sparse::Matrix
 *
 * - Agregué que cuando relocaliza, imprima la matriz de rototraslación.
 *
 */

int main(int argc, char **argv){

	cout	<< "Iniciando ORB-SLAM.  Línea de comando:" << endl
			<< "os1 [archivo de configuración yaml [ruta al archivo de video]]\nSin argumentos para usar la webcam, con configuración en webcam.yaml" << endl;

    // Parámetros de la línea de comando

    char* rutaConfiguracion = NULL;
    char* rutaVideo = NULL;
	char archivoConfiguracionWebcamPorDefecto[] = "/home/toams/facultad/os1/webcamNacho.yaml";	// Configuración por defecto, para webcam.

    switch(argc){
	case 1:	// Sin argumentos, webcam por defecto y webcam.yaml como configuración
		rutaConfiguracion = archivoConfiguracionWebcamPorDefecto;
		cout << "Sin argumentos, webcam con esta configuración: " << rutaConfiguracion << endl;
		break;

	case 2:	// Un argumento, archivo de configuración  NO IMPLEMENTADO
		rutaConfiguracion = argv[1];
		break;

	case 3:	// Dos argumentos, archivo de configuración y ruta de video
		rutaConfiguracion = argv[1];
		rutaVideo = argv[2];	// Segundo argumento
		cout << "Dos argumentos: " << rutaConfiguracion << ", " << rutaVideo << endl;
		break;

	}

	// Inicializa el sistema SLAM.
    // Mi versión de archivo binario con el vocabulario, que carga mucho más rápido porque evita el análisis sintáctico.
    ORB_SLAM2::System SLAM("/home/toams/facultad/os1/orbVoc.bin", rutaConfiguracion,ORB_SLAM2::System::MONOCULAR,true);

    // Puntero global al sistema singleton
    Sistema = &SLAM;

    // Imagen de entrada
    cv::Mat im;

    ORB_SLAM2::Viewer* visor = SLAM.mpViewer;

    // Arranca el hilo de Video
    ORB_SLAM2::Video video;
    new thread(&ORB_SLAM2::Video::Run, &video);

	// Indica si la entrada de video corresponde a un archivo, y por lo tanto su base de tiempo es controlable
	bool videoEsArchivo = rutaVideo;
    if(videoEsArchivo){
		video.abrirVideo(rutaVideo);
		visor->setDuracion(video.cantidadCuadros);
	}else{
		// No hay parámetros, no hay video, sólo webcam.
		video.abrirCamara(2);
		visor->setDuracion();
	}

    cout << "Recibiendo imagen" << endl;

    TcpSocketImageDecoder tcid;
    cv::Mat img = tcid.receiveImage();
    cout << "Imagen recibida" << endl;

    // cv::Mat img = cv::imread("../photo2.jpg", CV_LOAD_IMAGE_GRAYSCALE);

    bool hasPrintedMatrix = false;
    bool hasInitialized = false;
    bool mapaCargado = false;
    int amountOfFramesToRelocate = 0;
    bool imageHasBeenSent = true;
	cv::Mat initialMatrix;
    visor->cargarMapa = true;

    while(true){

        // Leer nuevo cuadro, controlar el tiempo del video
    	if(video.flujo == ORB_SLAM2::Video::VIDEO || video.flujo == ORB_SLAM2::Video::VIDEO_RT){
    		if(visor->tiempoAlterado){
				// El usuario movió el trackbar: hay que cambiar el frame.
				video.setCuadroPos(visor->tiempo);
				visor->tiempoAlterado = false;	// Bajar la señal.
			} else if(visor->tiempoReversa && !visor->videoPausado){
				// La película va marcha atrás
				if(video.posCuadro<2){
					// Si llega al inicio del video, recomienza hacia adelante
					video.setCuadroPos(0);
					visor->tiempoReversa = false;
				} else {
					video.setCuadroPos(video.posCuadro-1);
				}
			}
    	}

    	// t1 está en segundos, con doble precisión.  El bucle para inicialización demora entre 1 y 2 centésimas de segundo.
    	std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();

        // Pass the image to the SLAM system
        if(video.imagenDisponible || imageHasBeenSent) {
            // todos los if son míos
            if (mapaCargado && SLAM.mpTracker->mState == 3) {
                amountOfFramesToRelocate = amountOfFramesToRelocate + 1;
            }

            if (SLAM.mpTracker->mState == 2 && !hasInitialized) {
                hasInitialized = !hasInitialized;
                initialMatrix = SLAM.mpTracker->mCurrentFrame.mTcw.inv();
                cout << "La matriz de rototraslación inicial es: \n" << endl;
                cout << initialMatrix << endl;
            }
            if (SLAM.mpTracker->mState == 2 && SLAM.mpTracker->mbOnlyTracking && !hasPrintedMatrix) {
                hasPrintedMatrix = !hasPrintedMatrix;
                cv::Mat inverse = SLAM.mpTracker->mCurrentFrame.mTcw.inv();
                cout << "La mtriz de rototraslación de la posición localizada es: \n" << endl;
				cout << inverse << endl;
				cout << "Necesitó" << endl;
				cout << amountOfFramesToRelocate << endl;
                cout << "El vector de traslación es: " << endl;
				cv::Mat vector = inverse.inv()*(initialMatrix.col(3)).rowRange(0, 2);
                cout << vector << endl;

                float x = vector.at<float>(0, 0);
				float y = vector.at<float>(1, 0);
				float z = vector.at<float>(2, 0);

                std::string str= to_string(x) +" "+ to_string(y) +" "+ to_string(z);
                /* Escribir respuesta acá */
                tcid.sendLocation("localhost", 7001, str);
                /* Coordenada Z para adelante, X para la derecha e Y para abajo*/
            }

            SLAM.TrackMonocular(img , (double) video.posCuadro);
            // SLAM.TrackMonocular(video.getImagen(), (double) video.posCuadro);

        }
    	// Ver si hay señal para cargar el mapa, que debe hacerse desde este thread
    	if(visor->cargarMapa){
    		visor->cargarMapa = false;

            SLAM.mpTracker->mState = ORB_SLAM2::Tracking::NOT_INITIALIZED;
    		// El reset subsiguiente requiere que LocalMapping no esté pausado.
    		SLAM.mpLocalMapper->Release();

        	// Limpia el mapa de todos los singletons
    		SLAM.mpTracker->Reset();
    		// En este punto el sistema está reseteado.

    	    // Espera a que se detenga LocalMapping y  Viewer
    		SLAM.mpLocalMapper->RequestStop();
    		SLAM.mpViewer	  ->RequestStop();

        	char charchivo[1024];
        	//FILE *f = popen("zenity --file-selection", "r");
            //fgets(charchivo, 1024, f);
            char* chararchivo = "a";
        	if(charchivo[0]){
            	while(!SLAM.mpLocalMapper->isStopped()) usleep(1000);
    			while(!SLAM.mpViewer	 ->isStopped()) usleep(1000);

				//std::string nombreArchivo(charchivo);
				//nombreArchivo.pop_back();	// Quita el \n final
                std::string nombreArchivo("/home/toams/facultad/os1/pieza.osMap");
                cout << "Abriendo archivo " << nombreArchivo << endl;
				SLAM.serializer->mapLoad(nombreArchivo);
				cout << "Mapa cargado." << endl;

        	}
			SLAM.mpTracker->mState = ORB_SLAM2::Tracking::LOST;

			// Reactiva viewer.  No reactiva el mapeador, pues el sistema queda en sólo tracking luego de cargar.
			SLAM.mpViewer->Release();

			// Por las dudas, es lo que hace Tracking luego que el estado pase a LOST.
			// Como tiene un mutex, por las dudas lo invoco después de viewer.release.
			SLAM.mpFrameDrawer->Update(SLAM.mpTracker);
            mapaCargado = true;
            SLAM.mpTracker->mbOnlyTracking = true;
    	}
    	if(visor->guardarMapa){
    		visor->guardarMapa = false;

    	    // Espera a que se detenga LocalMapping
    		SLAM.mpLocalMapper->RequestStop();
    		SLAM.mpViewer	  ->RequestStop();	// No parece que sea necesario para guardar, sino sólo para cargar, pues al guardar no se modifica el mapa.

        	//char archivo[] = "mapa.bin";
        	char charchivo[1024];
        	FILE *f = popen("zenity --file-selection --save --confirm-overwrite --filename=mapa.osMap", "r");
        	fgets(charchivo, 1024, f);

        	if(charchivo[0]){
        		while(!SLAM.mpLocalMapper->isStopped()) usleep(1000);
        		while(!SLAM.mpViewer	 ->isStopped()) usleep(1000);

				std::string nombreArchivo(charchivo);
				nombreArchivo.pop_back();	// Quita el \n final
				cout << "Guardando archivo " << nombreArchivo << endl;

            	SLAM.serializer->mapSave(nombreArchivo);
            	cout << "Mapa guardado." << endl;
        	}

        	// Reactiva viewer.  No reactiva el mapeador, pues el sistema queda en sólo tracking luego de cargar.
        	SLAM.mpViewer->Release();
    	}


    	// Abrir un archivo de video
    	if(visor->abrirVideo){
    		visor->abrirVideo = false;
        	char charchivo[1024];
        	FILE *f = popen("zenity --file-selection", "r");
        	fgets(charchivo, 1024, f);
        	if(charchivo[0]){
				std::string nombreArchivo(charchivo);
				nombreArchivo.pop_back();	// Quita el \n final
				cout << "Abriendo video " << nombreArchivo << endl;
				video.abrirVideo(nombreArchivo);
				cout << "Video abierto." << endl;

				// Abrir archivo de calibración, que es igual al de configuración pero sólo se leen los parámetros de cámara
				Sistema->mpTracker->ChangeCalibration(
						// Hay que cambiar la extensión del nombre del archivo de video por .yaml
						(nombreArchivo.erase(nombreArchivo.find_last_of('.'))).append(".yaml")
				);
        	}

    	}

        // Stop cronómetro para medir duración del procesamiento
        double ttrack = std::chrono::duration_cast<std::chrono::duration<double> >(std::chrono::steady_clock::now() - t1).count();

        // Delay para 30 fps, período de 0.033 s
        if(ttrack < 0.033)
        	usleep((0.033-ttrack)*1e6);

    }
    cout << "Invocando shutdown." << endl;

    // Stop all threads
    SLAM.Shutdown();

    cout << "Terminado." << endl;

    return 0;
}
