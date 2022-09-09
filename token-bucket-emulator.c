#include <stdio.h>
#include <math.h>

#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <stdbool.h>
#include "cs402.h"
#include <ctype.h>
#include <errno.h>

#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <pthread.h>

#include "my402list.h"

struct timeval startTime;
pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
sigset_t set;

// rate of packets
// mode based
double lambdaPacketsPerSecond = 1;

bool allPacketsStarted = false;
bool timeToQuit = false;

int remainingTokens = 0;
int noOfPacks = 20;
int noOfPacksProduced = 0;
int noOfTokens = 0;

My402List q1, q2;

// server rate of packets
// mode based
double muPacketsPerSecond = 0.35;

// rate of tokens
double rTokensPerSecond = 1.5;

// bucket depth
int bTokens = 10;

// number of tokens needed in a packet
// mode based
int pTokens = 3;

int mode = 1;
char *traceFile;
FILE *fp;

double packageInterArrivalTime = 0;
double packageServiceTime = 0;
int servicedPacket = 0;
long double packageQ1Time = 0;
long double packageQ2Time = 0;

long double packageS1Time = 0;
long double packageS2Time = 0;

double packageSystemTime = 0;
double packageSystemTimeSquare = 0;

double totalDroppedTokens = 0;
double totalDroppedPackets = 0;

pthread_t tokenThread;
pthread_t packetThread;

long double getDiff()
{
	struct timeval nowTime, res;
	gettimeofday(&nowTime, NULL);

	timersub(&nowTime, &startTime, &res);

	long double val = res.tv_sec * 1000000 + res.tv_usec;
	return val;
}

long getDiffFrom(struct timeval sTime)
{
	struct timeval nowTime, res;
	gettimeofday(&nowTime, NULL);

	timersub(&nowTime, &sTime, &res);

	long double val = res.tv_sec * 1000000 + res.tv_usec;
	return val;
}

typedef struct tagPacket
{
	int tokensNeeded;
	long serviceTimeInMicroSeconds;
	long double arrivalTime;
	long double q1Entry;
	long double q1Exit;
	long double q2Entry;
	long double q2Exit;
	long serviceEntry;
	int id;
} My402Packet;

double parseDouble(int argc, char *argv[], int i)
{
	if ((i + 1) < argc && argv[i + 1][0] != '-')
	{
		double dval = (double)0;
		if (sscanf(argv[i + 1], "%lf", &dval) != 1)
		{
			/* cannot parse argv[2] to get a double value */
			fprintf(stderr, "Malformed command, cannot parse double value for %s \n", argv[i]);
			exit(1);
		}
		else
		{
			/* success */
			return dval;
		}
	}
	else
	{
		fprintf(stderr, "Malformed command, value for commandline option %s was not given \n", argv[i]);
		exit(1);
	}
}

int parseInt(int argc, char *argv[], int i)
{
	if ((i + 1) < argc && argv[i + 1][0] != '-')
	{
		int ival = (int)0;
		if (sscanf(argv[i + 1], "%d", &ival) != 1)
		{
			/* cannot parse argv[2] to get a double value */
			fprintf(stderr, "Malformed command, cannot parse int value for %s \n", argv[i]);
			exit(1);
		}
		else
		{
			/* success */
			return ival;
		}
	}
	else
	{
		fprintf(stderr, "Malformed command, value for commandline option %s was not given \n", argv[i]);
		exit(1);
	}
}

void *packet()
{
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);
	long double sleepTime;
	int parseSleepTime;
	long serviceTimeInMicro = 1000000 / muPacketsPerSecond;
	char buf[2000];

	if (mode == 1)
	{
		sleepTime = 1000 / lambdaPacketsPerSecond;
		if (round(sleepTime) == 0)
		{
			sleepTime = 0;
		}
		else
		{
			sleepTime *= 1000;
		}
	}

	long double arrivalTime, previousArrivalTime = 0;

	char *token;
	for (int i = 1; i <= noOfPacks; i++)
	{
		My402Packet *ptr = (My402Packet *)malloc(sizeof(My402Packet));
		if (mode == 1)
		{
			ptr->serviceTimeInMicroSeconds = serviceTimeInMicro;
			ptr->tokensNeeded = pTokens;
			ptr->id = i;
		}
		else
		{
			if (fgets(buf, 2000, fp) != NULL)
			{
				token = strtok(buf, " ");
				if (token == NULL)
				{
					fprintf(stderr, "Tracefile on line %d has incorrect format for inter-arrival time of packet", (i + 1));
					exit(1);
				}

				if (sscanf(token, "%d", &parseSleepTime) != 1)
				{
					fprintf(stderr, "Could not parse inter-arrival time of packet on line %d \n", (i + 1));
					exit(1);
				}

				sleepTime = parseSleepTime * 1000;

				token = strtok(NULL, " ");
				if (token == NULL)
				{
					fprintf(stderr, "Tracefile on line %d has incorrect format for no of tokens of packet", (i + 1));
					exit(1);
				}

				if (sscanf(token, "%d", &ptr->tokensNeeded) != 1)
				{
					fprintf(stderr, "Could not parse no of tokens of packet on line %d \n", (i + 1));
					exit(1);
				}

				token = strtok(NULL, " ");
				if (token == NULL)
				{
					fprintf(stderr, "Tracefile on line %d has incorrect format for service time of packet", (i + 1));
					exit(1);
				}

				int serviceTimeInt;
				if (sscanf(token, "%d", &serviceTimeInt) != 1)
				{
					fprintf(stderr, "Could not parse service time of packet on line %d \n", (i + 1));
					exit(1);
				}
				ptr->serviceTimeInMicroSeconds = serviceTimeInt * 1000;
				ptr->id = i;
			}
			else
			{
				break;
			}
		}

		if (sleepTime != 0)
		{
			pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
			usleep(sleepTime);
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);
		}

		pthread_mutex_lock(&m);

		if (timeToQuit)
		{
			pthread_mutex_unlock(&m);
			break;
		}

		arrivalTime = getDiff();
		ptr->arrivalTime = arrivalTime;
		if (ptr->tokensNeeded > bTokens)
		{
			fprintf(stdout, "%012.3Lfms: p%d arrives, needs %d tokens, inter-arrival time = %.3Lfms, dropped \n", arrivalTime / 1000.0, i, ptr->tokensNeeded,
					(arrivalTime - previousArrivalTime) / 1000);
			totalDroppedPackets++;

			packageInterArrivalTime = ((i - 1) * packageInterArrivalTime + (arrivalTime - previousArrivalTime)) / i;

			previousArrivalTime = arrivalTime;
			noOfPacksProduced++;

			pthread_mutex_unlock(&m);
			continue;
		}

		fprintf(stdout, "%012.3Lfms: p%d arrives, needs %d tokens, inter-arrival time = %.3Lfms \n", arrivalTime / 1000, i, ptr->tokensNeeded,
				(arrivalTime - previousArrivalTime) / 1000);

		// all 3 types of packets - completed, removed, dropped
		packageInterArrivalTime = ((i - 1) * packageInterArrivalTime + (arrivalTime - previousArrivalTime)) / i;

		previousArrivalTime = arrivalTime;
		noOfPacksProduced++;

		long double entryTime = getDiff();
		fprintf(stdout, "%012.3Lfms: p%d enters Q1\n", entryTime / 1000, i);
		ptr->q1Entry = entryTime;

		if (My402ListEmpty(&q1) && remainingTokens >= ptr->tokensNeeded)
		{
			remainingTokens -= ptr->tokensNeeded;
			long double exitTime = getDiff();
			ptr->q1Exit = exitTime;
			fprintf(stdout, "%012.3Lfms: p%d leaves Q1, time in Q1 = %.3Lfms, token bucket now has %d token\n", exitTime / 1000, i, (exitTime - entryTime) / 1000, remainingTokens);

			(void)My402ListAppend(&q2, ptr);
			long double entryTimeQ2 = getDiff();
			fprintf(stdout, "%012.3Lfms: p%d enters Q2 \n", entryTimeQ2 / 1000, i);
			ptr->q2Entry = entryTimeQ2;

			pthread_cond_broadcast(&cv);
		}
		else
		{
			(void)My402ListAppend(&q1, ptr);
		}

		pthread_mutex_unlock(&m);
	}
	pthread_mutex_lock(&m);
	allPacketsStarted = true;
	if (My402ListLength(&q2) == 0 && My402ListLength(&q1) == 0)
	{
		timeToQuit = true;
		pthread_cond_broadcast(&cv);
	}
	pthread_mutex_unlock(&m);

	pthread_exit((void *)0);
}

void *token()
{
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);
	long double sleepTime;
	sleepTime = 1000 / rTokensPerSecond;

	if (round(sleepTime) == 0)
	{
		sleepTime = 0;
	}
	else
	{
		sleepTime *= 1000;
	}

	for (int i = 1;; i++)
	{
		noOfTokens = i - 1;
		if (My402ListEmpty(&q1) && allPacketsStarted)
		{
			break;
		}

		if (sleepTime != 0)
		{
			pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
			usleep(sleepTime);
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);
		}

		pthread_mutex_lock(&m);

		if (timeToQuit)
		{
			pthread_mutex_unlock(&m);
			break;
		}

		if (remainingTokens != bTokens)
		{
			remainingTokens++;
			if (remainingTokens == 1)
			{
				fprintf(stdout, "%012.3Lfms: token t%d arrives, token bucket now has %d token\n", getDiff() / 1000, i, remainingTokens);
			}
			else
			{
				fprintf(stdout, "%012.3Lfms: token t%d arrives, token bucket now has %d tokens\n", getDiff() / 1000, i, remainingTokens);
			}
		}
		else
		{
			fprintf(stdout, "%012.3Lfms: token t%d arrives, dropped \n", getDiff() / 1000, i);
			totalDroppedTokens++;
			pthread_mutex_unlock(&m);
			continue;
		}

		if (My402ListEmpty(&q1) == 0)
		{
			My402Packet *head = (My402Packet *)My402ListFirst(&q1)->obj;
			if (remainingTokens == head->tokensNeeded)
			{
				My402ListElem *elem = My402ListFirst(&q1);
				My402ListUnlink(&q1, elem);

				long double exitTime = getDiff();
				head->q1Exit = exitTime;
				fprintf(stdout, "%012.3Lfms: p%d leaves Q1, time in Q1 = %.3Lfms, token bucket now has 0 token\n", exitTime / 1000, head->id, (exitTime - head->q1Entry) / 1000);

				My402ListAppend(&q2, head);
				long double entryTimeQ2 = getDiff();
				fprintf(stdout, "%012.3Lfms: p%d enters Q2 \n", entryTimeQ2 / 1000, head->id);
				head->q2Entry = entryTimeQ2;

				pthread_cond_broadcast(&cv);

				remainingTokens = 0;
			}
		}
		pthread_mutex_unlock(&m);
	}
	pthread_exit((void *)0);
}

void *server(void *arg)
{
	int serverId = (int)arg;
	double systemTime;

	for (;;)
	{
		pthread_mutex_lock(&m);

		if ((allPacketsStarted && My402ListLength(&q1) == 0 && My402ListLength(&q2) == 0) || timeToQuit)
		{
			pthread_mutex_unlock(&m);
			break;
		}

		while (My402ListLength(&q2) == 0 && !timeToQuit)
		{
			pthread_cond_wait(&cv, &m);
		}

		if (timeToQuit)
		{
			pthread_mutex_unlock(&m);
			break;
		}

		My402ListElem *elem = My402ListFirst(&q2);
		My402Packet *packet = (My402Packet *)elem->obj;

		long double exitTimeQ2 = getDiff();
		packet->q2Exit = exitTimeQ2;
		fprintf(stdout, "%012.3Lfms: p%d leaves Q2, time in Q2 = %.3Lfms \n", exitTimeQ2 / 1000, packet->id, (exitTimeQ2 - packet->q2Entry) / 1000);
		My402ListUnlink(&q2, elem);

		long double serviceStart = getDiff();
		packet->serviceEntry = serviceStart;
		fprintf(stdout, "%012.3Lfms: p%d begins service in S%d, requesting %ldms of service \n", serviceStart / 1000, packet->id, serverId,
				packet->serviceTimeInMicroSeconds / 1000);
		pthread_mutex_unlock(&m);

		usleep(packet->serviceTimeInMicroSeconds);

		pthread_mutex_lock(&m);
		long double serviceEnd = getDiff();
		fprintf(stdout, "%012.3Lfms: p%d departs from S%d, service time = %.3Lfms, time in system = %.3Lfms \n", serviceEnd / 1000, packet->id, serverId,
				(serviceEnd - serviceStart) / 1000, (serviceEnd - packet->arrivalTime) / 1000);

		// only completed packets
		packageServiceTime = (servicedPacket * packageServiceTime + (serviceEnd - serviceStart)) / (servicedPacket + 1);

		// only completed packets
		packageQ1Time += (packet->q1Exit - packet->q1Entry);
		packageQ2Time += (packet->q2Exit - packet->q2Entry);

		if (serverId == 1)
		{
			packageS1Time += (serviceEnd - serviceStart);
		}
		else
		{
			packageS2Time += (serviceEnd - serviceStart);
		}

		// only completed packets
		systemTime = (serviceEnd - packet->arrivalTime);
		packageSystemTime = (servicedPacket * packageSystemTime + systemTime) / (servicedPacket + 1);
		packageSystemTimeSquare = (servicedPacket * packageSystemTimeSquare + systemTime * systemTime) / (servicedPacket + 1);

		servicedPacket++;

		if (allPacketsStarted && My402ListLength(&q2) == 0 && My402ListLength(&q1) == 0)
		{
			timeToQuit = true;
			pthread_cond_broadcast(&cv);
			pthread_mutex_unlock(&m);
			break;
		}

		pthread_mutex_unlock(&m);
	}
	pthread_exit((void *)0);
}

void *monitor()
{
	int sig;

	while (1)
	{
		sigwait(&set, &sig);
		pthread_mutex_lock(&m);
		fprintf(stdout, "\n%012.3Lfms: SIGINT caught, no new packets or tokens will be allowed\n", getDiff() / 1000);
		timeToQuit = true;
		// cancel packet and token thread
		pthread_cancel(tokenThread);
		pthread_cancel(packetThread);

		pthread_cond_broadcast(&cv);

		pthread_mutex_unlock(&m);
		pthread_exit((void *)0);
	}
}

int main(int argc, char *argv[])
{
	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigprocmask(SIG_BLOCK, &set, 0);
	pthread_t thread;

	pthread_create(&thread, 0, monitor, 0);

	memset(&q1, 0, sizeof(My402List));
	(void)My402ListInit(&q1);

	memset(&q2, 0, sizeof(My402List));
	(void)My402ListInit(&q2);

	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-t") == 0)
		{
			mode = 2;
			break;
		}
	}

	double tempD;
	int tempI;
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-lambda") == 0)
		{
			tempD = parseDouble(argc, argv, i);
			lambdaPacketsPerSecond = mode == 1 ? tempD : lambdaPacketsPerSecond;
			i++;
		}
		else if (strcmp(argv[i], "-mu") == 0)
		{
			tempD = parseDouble(argc, argv, i);
			muPacketsPerSecond = mode == 1 ? tempD : muPacketsPerSecond;
			i++;
		}
		else if (strcmp(argv[i], "-r") == 0)
		{
			rTokensPerSecond = parseDouble(argc, argv, i);
			i++;
		}
		else if (strcmp(argv[i], "-B") == 0)
		{
			bTokens = parseInt(argc, argv, i);
			i++;
		}
		else if (strcmp(argv[i], "-P") == 0)
		{
			pTokens = mode == 1 ? parseInt(argc, argv, i) : pTokens;
			i++;
		}
		else if (strcmp(argv[i], "-n") == 0)
		{
			tempI = parseInt(argc, argv, i);
			noOfPacks = mode == 1 ? tempI : noOfPacks;
			i++;
		}
		else if (strcmp(argv[i], "-t") == 0)
		{
			if ((i + 1) < argc)
			{
				if (access(argv[i + 1], F_OK) != 0)
				{
					fprintf(stderr, "input file %s does not exist \n", argv[i + 1]);
					exit(1);
				}

				fp = fopen(argv[i + 1], "r");
				if (fp == NULL)
				{
					fprintf(stderr, "input file %s cannot be opened - access denies \n", argv[i + 1]);
					exit(1);
				}
				else
				{
					traceFile = argv[i + 1];
				}
			}
			else
			{
				fprintf(stderr, "Value for commandline option -t was not given \n");
				exit(1);
			}
			i++;
		}
		else
		{
			fprintf(stderr, "Malformed command, %s is not a valid commandline option \n", argv[i]);
			exit(1);
		}
	}

	char buffer[2000];

	if (mode == 2)
	{
		if (fgets(buffer, 2000, fp) != NULL)
		{
			if (sscanf(buffer, "%d", &noOfPacks) != 1)
			{
				fprintf(stderr, "malformed input - line 1 is not just a number \n");
				exit(1);
			}
		}
		else
		{
			fprintf(stderr, "input file %s is a directory or line 1 is not just a number \n", traceFile);
			exit(1);
		}
	}

	fprintf(stdout, "Emulation Parameters:\n");
	fprintf(stdout, "\t number to arrive = %d\n", noOfPacks);

	if (mode == 1)
	{
		fprintf(stdout, "\t lambda = %g\n", lambdaPacketsPerSecond);
		fprintf(stdout, "\t mu = %g\n", muPacketsPerSecond);
	}

	fprintf(stdout, "\t r = %g\n", rTokensPerSecond);
	fprintf(stdout, "\t B = %d\n", bTokens);

	if (mode == 1)
	{
		fprintf(stdout, "\t P = %d\n", pTokens);
	}
	else
	{
		fprintf(stdout, "\t tsfile = %s\n", traceFile);
	}

	if (0.1 > lambdaPacketsPerSecond && mode == 1)
	{
		lambdaPacketsPerSecond = 0.1;
	}

	if (0.1 > muPacketsPerSecond && mode == 1)
	{
		muPacketsPerSecond = 0.1;
	}

	if (0.1 > rTokensPerSecond)
	{
		rTokensPerSecond = 0.1;
	}

	//todo: Otherwise, please round the inter-token-arrival time to the nearest millisecond.

	pthread_t serverThread1;
	pthread_t serverThread2;

	fprintf(stdout, "00000000.000ms: emulation begins\n");
	gettimeofday(&startTime, NULL);

	pthread_create(&tokenThread, 0, token, NULL);
	pthread_create(&packetThread, 0, packet, NULL);
	pthread_create(&serverThread1, 0, server, (void *)1);
	pthread_create(&serverThread2, 0, server, (void *)2);

	pthread_join(tokenThread, 0);
	pthread_join(packetThread, 0);
	pthread_join(serverThread1, 0);
	pthread_join(serverThread2, 0);

	My402ListElem *elem;
	My402Packet *removedPacket;

	for (elem = My402ListFirst(&q1); elem != NULL; elem = My402ListNext(&q1, elem))
	{
		removedPacket = elem->obj;
		fprintf(stdout, "%012.3Lfms: p%d removed from Q1\n", getDiff() / 1000, removedPacket->id);
	}

	My402ListUnlinkAll(&q1);

	for (elem = My402ListFirst(&q2); elem != NULL; elem = My402ListNext(&q2, elem))
	{
		removedPacket = elem->obj;
		fprintf(stdout, "%012.3Lfms: p%d removed from Q2\n", getDiff() / 1000, removedPacket->id);
	}

	My402ListUnlinkAll(&q2);

	long double emulationTime = getDiff();
	fprintf(stdout, "%012.3Lfms: emulation ends\n", emulationTime / 1000);
	fprintf(stdout, "\nStatistics:\n\n");

	if (noOfPacksProduced == 0)
	{
		fprintf(stdout, "\taverage packet inter-arrival time = (N/A, since no packet arrived) \n");
	}
	else
	{
		fprintf(stdout, "\taverage packet inter-arrival time = %.6g\n", packageInterArrivalTime / 1000000);
	}

	if (packageServiceTime == 0)
	{
		fprintf(stdout, "\taverage packet service time = (N/A, since no packet served) \n");
	}
	else
	{
		fprintf(stdout, "\taverage packet service time = %.6g\n", packageServiceTime / 1000000);
	}

	fprintf(stdout, "\taverage number of packets in Q1 = %.6Lg\n", (packageQ1Time / emulationTime));
	fprintf(stdout, "\taverage number of packets in Q2 = %.6Lg\n", (packageQ2Time / emulationTime));

	fprintf(stdout, "\taverage number of packets at S1 = %.6Lg\n", (packageS1Time / emulationTime));
	fprintf(stdout, "\taverage number of packets at S2 = %.6Lg\n", (packageS2Time / emulationTime));

	if (packageSystemTime == 0)
	{
		fprintf(stdout, "\taverage time a packet spent in system = (N/A, no packet served)\n");
		fprintf(stdout, "\tstandard deviation for time spent in system = (N/A, no packet served)\n");
	}
	else
	{
		fprintf(stdout, "\taverage time a packet spent in system = %.6g\n", (packageSystemTime / 1000000));
		double variance = packageSystemTimeSquare - (packageSystemTime * packageSystemTime);
		fprintf(stdout, "\tstandard deviation for time spent in system = %.6g\n", sqrt(variance) / 1000000);
	}

	if (noOfTokens == 0)
	{
		fprintf(stdout, "\ttoken drop probability = N/A since no tokens were present \n");
	}
	else
	{
		fprintf(stdout, "\ttoken drop probability = %.6g\n", totalDroppedTokens / noOfTokens);
	}

	if (noOfPacksProduced == 0)
	{
		fprintf(stdout, "\tpacket drop probability = (N/A, since no packet served) \n");
	}
	else
	{
		fprintf(stdout, "\tpacket drop probability = %.6g\n", totalDroppedPackets / noOfPacksProduced);
	}

	return (0);
}