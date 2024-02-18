#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DIM_MAX 4096
#define SO_EOF -1
#define READ_END 0
#define WRITE_END 1

typedef struct SO_FILE {
	int fd;
	unsigned char buffer[DIM_MAX];
	int bufferPos;  // la ce pozitie ne aflam in buffer
	int bufferSize; // ce dim. are bufferul in acest moment
	char mode[3];   // r,w,a,r+,w+,a+
	char lastOperation[10];
	// char* error;
	int error;    // != 0 erori, 0 fara erori
	int feofFlag; // !=0 final, 0 nu am aj. final
	int fileCont; // contor de fisier (pozitie)
	int fileDim;  // dimensiunea fisierului, initializat la deschidere
	pid_t pid;

} SO_FILE;

void setLastOperation(SO_FILE *stream, const char *operation)
{
	//strcpy(stream->lastOperation, operation);
	memcpy(stream->lastOperation, operation, strlen(operation)+1);

	strcat(stream->lastOperation, "\0");
}

int so_fflush(SO_FILE *stream)
{
	if (stream == NULL || stream->fd == -1) {
		stream->error = 1;
		return SO_EOF;
	}
	if (strcmp(stream->lastOperation, "fputc") == 0 ||
		strcmp(stream->lastOperation, "fwrite") == 0) {
		if (stream->bufferPos > 0) {
			int ret = write(stream->fd, stream->buffer, stream->bufferPos);

			if (ret < 0) {
				stream->error = 1;
				free(stream);
				perror("Eroare la executarea fflush");
				return SO_EOF;
			}
			stream->bufferPos = 0;
		}
	}
	stream->bufferPos = 0;
	return 0;
}

int so_fseek(SO_FILE *stream, long offset, int whence)
{
	// verificam daca ultima operatie a fost de scriere, daca a fost facem fflush
	// (tranfer continut buffer in fisier si apoi facem lseek)
	if (stream == NULL || stream->fd == -1) {
		stream->error = 1;
		return -1;
	}
	if (strcmp(stream->lastOperation, "fwrite") == 0 ||
		strcmp(stream->lastOperation, "fputc") == 0) {
		int status = so_fflush(stream);

		if (status == SO_EOF) {
			perror("Eroare fseek\n");
			stream->error = 1;
			return SO_EOF;
		}
	}
	if (strcmp(stream->lastOperation, "fread") == 0 ||
		strcmp(stream->lastOperation, "fgetc") == 0) {
		stream->bufferPos = 0;
	}
	int pos = lseek(stream->fd, offset, whence);

	if (pos < 0) {
		perror("Eroare lseek");
		stream->error = 1;
		return SO_EOF;
	}
	stream->fileCont = pos;

	return 0;
}

long so_ftell(SO_FILE *stream)
{
	if (stream == NULL || stream->fd == -1) {
		stream->error = 1;
		return -1;
	}
	return stream->fileCont;
}

int so_fgetc(SO_FILE *stream)
{

	if (stream == NULL || stream->fd == -1) {
		stream->error = 1;
		return SO_EOF;
	}
	if (stream->fileDim == so_ftell(stream)) {
		stream->feofFlag = 1;
		//		return SO_EOF;
	}
	if (!(strcmp(stream->mode, "r") == 0 || strcmp(stream->mode, "r+") == 0 ||
			strcmp(stream->mode, "w+") == 0 || strcmp(stream->mode, "a+") == 0)) {
		perror("Modul fisierului nu permite citire");
		stream->error = 1;
		return SO_EOF;
	}

	if (stream->bufferPos == 0 || stream->bufferPos == stream->bufferSize ||
		strcmp(stream->lastOperation, "fputc") == 0 ||
		strcmp(stream->lastOperation, "fwrite") == 0) {
		int bytesRead = read(stream->fd, stream->buffer, DIM_MAX);

		stream->bufferSize = bytesRead;
		// printf("TEST:realizare apel read, bytes cititi: %d\n", bytesRead);
		if (bytesRead < 0) {
			perror("Eroare read fgetc");
			stream->error = 1;
			return SO_EOF;
		}

		setLastOperation(stream, "fgetc");
		stream->bufferPos = 0;
	}

	if ((strcmp(stream->lastOperation, "fputc") == 0 ||
		strcmp(stream->lastOperation, "fwrite") == 0) &&
		(strcmp(stream->mode, "r+") == 0 || strcmp(stream->mode, "w+") == 0 ||
		strcmp(stream->mode, "a+") == 0)) {

		int status = so_fseek(stream, 0, SEEK_SET);

		if (status == SO_EOF) {
			stream->error = 1;
			return SO_EOF;
		}
	}

	if (stream->feofFlag == 1) {
		perror("Am ajuns la finalul fisierului");

		return SO_EOF;
	}

	unsigned char character = stream->buffer[stream->bufferPos];

	stream->bufferPos++;

	int cast = (int)(unsigned char)character;

	setLastOperation(stream, "fgetc");
	stream->fileCont++;

	return cast;
	}
int so_fputc(int c, SO_FILE *stream)
{
	// se poate scrie in fisier doar daca este deschis
	// in modurile r+,w,w+,a,a+
	if (stream == NULL || stream->fd == -1) {
		stream->error = 1;
		return SO_EOF;
	}
	if (!(strcmp(stream->mode, "r+") == 0 || strcmp(stream->mode, "w+") == 0 ||
			strcmp(stream->mode, "a") == 0 || strcmp(stream->mode, "a+") == 0 ||
			strcmp(stream->mode, "w") == 0)) {
		stream->error = 1;
		perror("Modul fisierului nu permite scriere fputc");
		return SO_EOF;
	}
	if (strcmp(stream->mode, "r+") == 0 || strcmp(stream->mode, "w+") == 0 ||
		strcmp(stream->mode, "a+") == 0) {
		if (strcmp(stream->lastOperation, "fread") == 0 ||
			strcmp(stream->lastOperation, "fgetc") == 0) {
			int status = so_fseek(stream, 0, SEEK_CUR);

			if (status == SO_EOF) {
				perror("Eroare flush in fputc");
				stream->error = 1;
				return SO_EOF;
			}
			stream->fileCont = status;
		}
	}
	// buffer plin => scriere in fisier
		if (stream->bufferPos == DIM_MAX) {
			int totalWritten = 0;
			int bytesWritten;

			while (totalWritten != DIM_MAX) {
				bytesWritten = write(stream->fd, stream->buffer + totalWritten,
									DIM_MAX - totalWritten);

				// verificare ce scriem
				//	for(int i =  totalWritten; i<totalWritten + bytesWritten ; i++)
				//		printf("0x%x ", (int)stream->buffer[i]);
				if (bytesWritten < 0) {
					stream->error = 1;
					return SO_EOF;
				}
				if (bytesWritten == 0)
					break;
				totalWritten += bytesWritten;
			}
		if (bytesWritten < 0) {
			stream->error = 1;
			perror("Eroare fputc\n");
			return SO_EOF;
		}
		stream->bufferPos = 0;
		stream->feofFlag = 0;
	}

	stream->buffer[stream->bufferPos++] = (unsigned char)c;
	stream->fileCont++;
	stream->fileDim++;
	setLastOperation(stream, "fputc");
	return c;
	}

int so_ferror(SO_FILE *stream)
{
	return stream->error;
}
int so_fileno(SO_FILE *stream)
{
	return stream->fd;
}

// return != 0 end of file return 0 daca nu suntem la final
int so_feof(SO_FILE *stream)
{
	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}
	return stream->feofFlag;
}

void initiateFile(SO_FILE *stream, const char *mode)
{

	stream->bufferPos = 0;
	stream->error = 0;
	stream->feofFlag = 0;
	stream->fileCont = 0;
	//	strcpy(stream->mode, mode);
	memcpy(stream->mode, mode, strlen(mode)+1);
	//stream->mode[strlen(stream->mode)] = 0;

	setLastOperation(stream, "open");

	for (int i = 0; i < DIM_MAX; i++)
		stream->buffer[i] = 0;

	int curr = lseek(stream->fd, 0, SEEK_CUR);

	stream->fileDim = lseek(stream->fd, 0, SEEK_END);
	lseek(stream->fd, curr, SEEK_SET);
	if (strcmp(mode, "r") == 0 || strcmp(mode, "r+") == 0 ||
		strcmp(mode, "w") == 0 || strcmp(mode, "w+") == 0) {
		so_fseek(stream, 0, SEEK_SET);
	}
}
SO_FILE *so_fopen(const char *pathname, const char *mode)
{
	SO_FILE *stream = (SO_FILE *)malloc(sizeof(SO_FILE));

	if (stream == NULL) {
		perror("Eroare alocare de memorie\n");

		free(stream);
		return NULL;
	}

	if (strcmp(mode, "r") == 0) {
		stream->fd = open(pathname, O_RDONLY);
	} else if (strcmp(mode, "r+") == 0) {
		stream->fd = open(pathname, O_RDWR);
	} else if (strcmp(mode, "w") == 0) {
		stream->fd = open(pathname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	} else if (strcmp(mode, "w+") == 0) {
		stream->fd = open(pathname, O_RDWR | O_TRUNC | O_CREAT, 0644);
	} else if (strcmp(mode, "a") == 0) {
		stream->fd = open(pathname, O_APPEND | O_WRONLY | O_CREAT, 0644);
	} else if (strcmp(mode, "a+") == 0) {
		stream->fd = open(pathname, O_APPEND | O_RDWR | O_CREAT, 0644);
	} else {
		stream->error = -1;
		free(stream);
		perror("Mod de deschidere inexistent");

		return NULL;
	}
	if (stream->fd < 0) {

		free(stream);
		perror("Eroare deschidere fisier");
		return NULL;
	}
	initiateFile(stream, mode);

	return stream;
}

int so_fclose(SO_FILE *stream)
{
	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}
	if (stream->fd <= 0) {
		stream->error = 1;
		free(stream);
		return SO_EOF;
	}

	int status = so_fflush(stream);

	if (status == SO_EOF)
		return SO_EOF;

	// stream->
	status = close(stream->fd);
	if (status == SO_EOF) {
		free(stream);
		return SO_EOF;
	}

	free(stream);
	stream = NULL;
	return 0;
}

size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{

	if (stream == NULL || size == 0 || nmemb == 0 || ptr == NULL) {
		perror("File pointer / content ptr NULL fwrite");
		stream->error = 1;
		return 0;
	}

	if (stream->fd <= 0) {
		stream->error = 1;
		return 0;
	}
	if (!(strcmp(stream->mode, "r+") == 0 || strcmp(stream->mode, "w+") == 0 ||
			strcmp(stream->mode, "a") == 0 || strcmp(stream->mode, "a+") == 0 ||
			strcmp(stream->mode, "w") == 0)) {
		stream->error = 1;
		return 0;
	}

	if (strcmp(stream->mode, "r+") == 0 || strcmp(stream->mode, "w+") == 0 ||
		strcmp(stream->mode, "a+") == 0) {
		if (strcmp(stream->lastOperation, "fread") == 0 ||
			strcmp(stream->lastOperation, "fgetc") == 0) {
			int status = so_fseek(stream, 0, SEEK_CUR);

			if (status == SO_EOF) {
				stream->error = 1;
				perror("Eroare flush in fwrite");
				return 0;
			}
		}
	}
	size_t totalElements = size * nmemb;
	size_t elementsWritten = 0;

	for (int i = 0; i < totalElements; i++) {
		int c = (int)((unsigned char *)ptr)[i];
		int status = so_fputc(c, stream);

		if (status == SO_EOF)
			stream->error = 1;
		elementsWritten++;
	}
	setLastOperation(stream, "fwrite");
	return elementsWritten / size;
}

size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	if (stream == NULL || size == 0 || nmemb == 0 || stream->feofFlag == 1) {
		stream->error = 1;
		return 0;
	}

	if (!(strcmp(stream->mode, "r") == 0 || strcmp(stream->mode, "r+") == 0 ||
			strcmp(stream->mode, "w+") == 0 || strcmp(stream->mode, "a+") == 0)) {
		perror("Eroare fread, nu suporta citire fisierul");
		stream->error = 1;
		return 0;
	}

	size_t totalElements = size * nmemb;
	size_t bytesRead = 0;
	int element;

	for (int i = 0; i < totalElements; i++) {
		element = so_fgetc(stream);
		if (stream->error == 1) {
			perror("Eroare fread");
			return 0;
		}
		if (element == -1 || stream->feofFlag == 1) {
			setLastOperation(stream, "fread");
			return bytesRead / size;
		}
		bytesRead++;
		((unsigned char *)ptr)[i] = (unsigned char)element;
		setLastOperation(stream, "fread");
	}
	setLastOperation(stream, "fread");
	return bytesRead / size;
}

	// r- fisier tip readonly, operatiile de citire exec. pe fisier citesc de la
	// citirea standard a proc. w - fisier tip wr.only, operatiile de scriere exec.
	// pe fisier scriu la intrarea standard a proc. creat copilul va executa comanda
	// specificata de command
SO_FILE *so_popen(const char *command, const char *type)
{
	if (command == NULL || type == NULL) {
		perror("Argumente nule popen: ");
		return NULL;
	}
	int pipe_fd[2];
	int fd = -1;
	int ret = pipe(pipe_fd);

	if (ret < 0) {
		perror("Eroare creare pipe: ");
		close(pipe_fd[WRITE_END]);
		close(pipe_fd[READ_END]);
		return NULL;
	}
	pid_t pid = fork();

	if (pid < 0) {
		perror("Eroare fork: ");
		close(pipe_fd[WRITE_END]);
		close(pipe_fd[READ_END]);
		return NULL;
	}

	if (pid == 0) { // copil
		if (strcmp(type, "w") == 0) {
			close(pipe_fd[WRITE_END]);
			dup2(pipe_fd[READ_END], STDIN_FILENO);
			close(pipe_fd[READ_END]);
		}
		if (strcmp(type, "r") == 0) {
			close(pipe_fd[READ_END]);
			dup2(pipe_fd[WRITE_END], STDOUT_FILENO);
			close(pipe_fd[WRITE_END]);
		}

		execlp("/bin/sh", "/bin/sh", "-c", command, NULL);
		perror("Eroare execlp: ");
		exit(0);
	}
	if (pid > 0) { // parinte
		if (strcmp(type, "w") == 0) {
			close(pipe_fd[READ_END]);
			fd = pipe_fd[WRITE_END];
		}
		if (strcmp(type, "r") == 0) {
			close(pipe_fd[WRITE_END]);
			fd = pipe_fd[READ_END];
		}
		// wait(NULL);
		exit(0);
	}

	SO_FILE *fileptr = (SO_FILE *)malloc(sizeof(SO_FILE));

	if (fileptr == NULL) {
		perror("Eroare alocare dinamica popen: ");
		// fileptr->error = 1;
		free(fileptr);
		return NULL;
	}
	initiateFile(fileptr, type);
	fileptr->pid = pid;
	fileptr->fd = fd;
	return fileptr;
}
int so_pclose(SO_FILE *stream)
{
	if (stream->pid == SO_EOF) {
		free(stream);
		return SO_EOF;
	}

	int status = so_fclose(stream);

	if (status < 0) {
		free(stream);
		return SO_EOF;
	}
	int ok = waitpid(stream->pid, &status, 0);

	if (ok < 0) {
		perror("Eroare asteptare proces copil");
		free(stream);
		return SO_EOF;
	}

	return status;

}
