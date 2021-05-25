#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <semaphore.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>

#define SUCCESS 0 
#define FAIL -1
#define DISCORD_NOTIFY 0                // To speed up the program, make this 0 to disable notifications
/*TO DO LIST : Wait function'ı timedwait ile değiştir*/

/*Condition Variables*/
pthread_cond_t ready_cond;              // Make sure every commentator is waiting for a question
pthread_cond_t questionAsked;           // To wake up commentators by broadcasting
pthread_cond_t thinking_finished;       // All commentators decide whether to answer or not
pthread_cond_t your_turn;               // To wake up one of the commentators to comment
pthread_cond_t commentator_finished;    // Commentator finishes to comment, wakes up moderator
pthread_cond_t breaking_news_create;    // To create breaking news
pthread_cond_t breaking_news_handle;    // To cut short commentator


pthread_mutex_t question_mutex;         // General mutex to provide mutual exclusion -- Two commentator should not speak at the same time

/*           Inputs          */
int question_count,commentator_count;
double probability,breaking_probability,time_bound;

int ready_count = 0;                    // To release questions, wait for every commentator
int thinking_finished_count=0;          // After releasing the question, commentators decide whether comment or not
int buffer_count=0;                     // Number of elements in the request queue
double current_time=0;                  // Time counter
char *time_as_string;                   // To print time stamp

int questions_finished=0;               // Boolean variable to cancel breaking news thread 
int currently_speaking=0;               // 1 if there is a commentator speaking, to make sure breaking news does not come when no one comments

float *buffer;                         // Request queue
float *time_sum;                        // Commentators speaking time array i.e. time_sum[0] is the total speaking time of the first commentator
int total_breaking_news=0;              // Total time spent on breaking news


/**
 * pthread_sleep takes an integer number of seconds to pause the current thread
 * original by Yingwu Zhu
 * updated by Muhammed Nufail Farooqi
 * updated by Fahrican Kosar
 */
int
pthread_sleep(double seconds){
    pthread_mutex_t mutex;
    pthread_cond_t conditionvar;
    if(pthread_mutex_init(&mutex,NULL)){
        return -1;
    }
    if(pthread_cond_init(&conditionvar,NULL)){
        return -1;
    }

    struct timeval tp;
    struct timespec timetoexpire;
    // When to expire is an absolute time, so get the current time and add
    // it to our delay time
    gettimeofday(&tp, NULL);
    long new_nsec = tp.tv_usec * 1000 + (seconds - (long)seconds) * 1e9;
    timetoexpire.tv_sec = tp.tv_sec + (long)seconds + (new_nsec / (long)1e9);
    timetoexpire.tv_nsec = new_nsec % (long)1e9;

    pthread_mutex_lock(&mutex);
    int res = pthread_cond_timedwait(&conditionvar, &mutex, &timetoexpire);
    pthread_mutex_unlock(&mutex);
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&conditionvar);

    //Upon successful completion, a value of zero shall be returned
    return res;
}

/*
    Obtained and modified from @Wim stackoverflow
*/
float RandomFloat(float lower_bound, float upper_bound) {
    float random = ((float) rand()) / (float) RAND_MAX;
    float diff = upper_bound - lower_bound;
    float r = random * diff;
    return lower_bound + r;
}

/*
  Log every update on the discord webhook
*/
int discord_log(char *message)
{
    if(DISCORD_NOTIFY!=1)
    {
        return 0;
    }

    int my_pid = fork();
    if(my_pid<0)
    {
        printf("Unsuccesful fork \n");
    }
    else if(my_pid==0)
    {
        execlp("apprise","-vv","-t","","-b",message,"discord://832961346240905276/fCUiUpYy8RMSXBJVmoQPwzCS64ZntPVnvbTdfzADH2yLwlplmuIK--3IfSVBPIwycOCT/",NULL);
        printf("Error\n");
        return FAIL;
    }
    else
    {
        wait(NULL);
        return SUCCESS;
    }
}

void swap(float *x,float *y)
{
    int temp = *x;
    *x = *y;
    *y = temp;
}

/*Selection sort*/
void sortArray(float arr[])
{
    int i, j, min;
 
    for (i = 0; i < commentator_count - 1; i++) {
 
        min = i;
        for (j = i + 1; j < commentator_count; j++)
            if (arr[j] < arr[min])
                min = j;
 
        swap(&arr[min], &arr[i]);
    }

}


/*Generate neat formatted time stamp i.e. [00:02]*/
char* timeStamp()
{
    int minute = (int)current_time /60 ;
    int seconds = (int)current_time%60;
    double miliseconds= current_time - (int)(current_time);
    int miliseconds_format= (int)(miliseconds*1000);
    if(minute==0)
    {
        if(seconds<10)
        {
            sprintf(time_as_string, "[00:0%d:%d]", seconds,miliseconds_format);
        }
        else
        {
            sprintf(time_as_string, "[00:%d:%d]", seconds,miliseconds_format);
        }
    }
    else
    {
        if(minute < 10)
        {
            if(seconds<10)
            {
                sprintf(time_as_string, "[0%d:0%d:%d]",minute, seconds,miliseconds_format);
            }
            else
            {
                sprintf(time_as_string, "[0%d:%d:%d]",minute, seconds,miliseconds_format);
            }
        }
        else
        {
            if(seconds<10)
            {
                sprintf(time_as_string, "[%d:0%d:%d]",minute, seconds,miliseconds_format);
            }
            else
            {
                sprintf(time_as_string, "[%d:%d:%d]",minute, seconds,miliseconds_format);
            }
        }

    }
    return time_as_string;
}

/*Breaking event handler*/
void *breakingNews()
{
    while(1)
    {
        pthread_mutex_lock(&question_mutex);
        /*Timed_wait to make sure program will not stuck at waiting after all questions finish*/
        static struct timespec time_to_wait = {0, 0};
        time_to_wait.tv_sec = time(NULL) + time_bound;
        if(pthread_cond_timedwait(&breaking_news_create,&question_mutex,&time_to_wait)==0)
        {
            currently_speaking=0;
            pthread_cond_signal(&breaking_news_handle);

            static struct timespec time_to_wait = {0, 0};
            time_to_wait.tv_sec = time(NULL) + time_bound;
            if(pthread_cond_timedwait(&breaking_news_handle,&question_mutex,&time_to_wait)==0)
            {
                total_breaking_news+=5;
                printf("%s Breaking news!\n",timeStamp());
                char breaking[64];
                sprintf(breaking, "%s Breaking news!",timeStamp());
                discord_log(breaking);
                sleep(5);
                current_time+=5;
                printf("%s Breaking news ends.\n",timeStamp());
                sprintf(breaking, "%s Breaking news ends!",timeStamp());
                discord_log(breaking);
                /*When there is a breaking news, make sure no new question asked by sending signal not in the commentator but after the breaking news*/
                buffer_count--;
                pthread_cond_signal(&commentator_finished); 
            }
        }

        pthread_mutex_unlock(&question_mutex);
        
        if(questions_finished)
        {
            return 0;
        }
    }

}


void *moderator()
{
    
    for (int i = 0; i < question_count; i++)
    {
        pthread_mutex_lock(&question_mutex);
        while (ready_count < commentator_count)
        {
            printf("Not enough commentator .. waiting \n");
            pthread_cond_wait(&ready_cond, &question_mutex);
        }
        /*Part A : Ask a question*/
        char question_state[64];
        sprintf(question_state, "%s Moderator asked Question %d",timeStamp(),i+1);
        discord_log(question_state);
        printf("%s\n",question_state);
        pthread_cond_broadcast(&questionAsked);
        ready_count = 0;

        /*Part B : Give turn to a commentator*/
        /*Wait for all commentators to decide whether to answer or not*/
        while(thinking_finished_count<commentator_count)
        {
            pthread_cond_wait(&thinking_finished,&question_mutex);
        }
        thinking_finished_count=0;

        /* Give turns to commentators and wait them to finish */
        /* If buffer_count <=0, either all commentators finished or no request has been made */
        while(buffer_count>0)
        {
            pthread_cond_signal(&your_turn);
            pthread_cond_wait(&commentator_finished,&question_mutex);
        }

        sprintf(question_state, "%s %d th question is finished",timeStamp(),i+1);
        discord_log(question_state);
        printf("%s\n",question_state);
        
        pthread_mutex_unlock(&question_mutex);
    }
    questions_finished=1;
}

void *commentator(void *args)
{
    int index = *(int*)args; 
    

    float sum_talk=0;
    char commentator_status[128];
    for(int i=0;i<question_count;i++)
    {
        pthread_mutex_lock(&question_mutex);
        /*Part A : Waiting for the question*/
        ready_count++;
        pthread_cond_signal(&ready_cond);
        pthread_cond_wait(&questionAsked, &question_mutex);

        /*Part B : Submitting the answer with an input probability*/
        int answer_prob = (rand() % 100) < probability;
        int buffer_index=-1;
        if(answer_prob == 0 )
        {
            printf("%s Commentator #%d does not want to answer \n",timeStamp(),index);
        }
        else
        {
            sprintf(commentator_status,"%s Commentator #%d generates answer, position in queue: %d",timeStamp(),index,buffer_count);
            discord_log(commentator_status);
            printf("%s\n",commentator_status);
            /*Send a request to the global queue*/
            buffer_index=buffer_count;
            float time = RandomFloat(1.0,time_bound);
            buffer[buffer_count++]= time;
            sum_talk+=buffer[buffer_count-1];
        }
        thinking_finished_count++;
        pthread_cond_signal(&thinking_finished);

        /*Part C: Commenting */
        /*No request submitted if buffer_index is -1 */
        if(buffer_index!=-1)
        {
            static struct timespec time_to_wait = {0, 0};
            /*When moderator gives the turn, speak for t seconds*/
            pthread_cond_wait(&your_turn,&question_mutex);
            sprintf(commentator_status,"%s Commentator #%d's turn to speak for %0.3f seconds ",timeStamp(),index,buffer[buffer_index]);
            discord_log(commentator_status);
            printf("%s\n",commentator_status);

            double sleep_time = buffer[buffer_index];
            long initial_time= time(NULL);
            time_to_wait.tv_sec = initial_time + sleep_time;
            currently_speaking=1;
            /*If equals to 0 --> there is a breaking news */
            if(pthread_cond_timedwait(&breaking_news_handle,&question_mutex,&time_to_wait)==0){
                sleep_time = time(NULL) - initial_time;
                current_time+=sleep_time;
                sprintf(commentator_status,"%s Commentator #%d is cut short due to a breaking news",timeStamp(),index);
                discord_log(commentator_status);
                printf("%s\n",commentator_status);
                /*Go to breaking news handler*/
                pthread_cond_signal(&breaking_news_handle);
            }
            else
            {
                current_time+=sleep_time;
                buffer_count--;
                pthread_cond_signal(&commentator_finished); 
            }

            
        }
        currently_speaking=0;
        pthread_mutex_unlock(&question_mutex);
    }

    time_sum[index]=sum_talk;

    free(args);
}

void getStatistics()
{
    printf("\n-----------------------------------------");
	printf("\n         Simulation Statistics");
	printf("\n-----------------------------------------\n\n");
    float sum=0;
    float mean,median=0;
    float min = time_sum[0];
    float max = time_sum[0];
    int min_index=0;
    int max_index=0;
    for(int i = 0 ;i< commentator_count;i++)
    {
        sum+=time_sum[i];
        if(time_sum[i]<min)
        {
            min = time_sum[i];
            min_index=i;
        }
        if(time_sum[i]>max)
        {
            max=time_sum[i];
            max_index=i;
        }
        printf("Commentator %d have spoken %f seconds\n",i,time_sum[i]);
    }
    mean= (float)sum/commentator_count;
    
    sortArray(time_sum);
    median = time_sum[commentator_count/2];

    /*Boxplot approach to analyze the distribution of time periods*/
    int first_quartile = time_sum[commentator_count/4];
    printf("First Quartile is %d \n",first_quartile);
    int third_quartile = time_sum[commentator_count*3/4];
    printf("Third quartile is %d \n",third_quartile);
    float inter_qr = third_quartile - first_quartile;
    float offset = 3.0/2*inter_qr;
    float upper_bound = third_quartile + offset; 
    printf("Upper bound is %f\n",upper_bound);
    float lower_bound = first_quartile - offset;
    printf("Lower bound is %f \n",lower_bound);
    int outlier_count = 0;

    int fairness=1;
    /*Determine outliers*/
    for(int i = 0 ; i< commentator_count;i++)
    {
        int value = time_sum[i];
        if(value < lower_bound) outlier_count++;
        if(value > upper_bound) outlier_count++;
    }
    if(outlier_count>0)
    {
        fairness=0;
    }
    /*The divergence between first and third quartile*/
    /*Only 25%*probability is allowed for this program, exceeding implies the program was not fair*/
    float max_bound = time_bound * commentator_count * probability/100;
    float allowed_ceil = (float)max_bound /4;

    int inclined_to_median,inclined_to_quartile=0; 
    for(int i = commentator_count/4+1;i<commentator_count/2;i++)
    {
        int value = time_sum[i];
        if(median - value < value-first_quartile)
        {
            inclined_to_median++;
        }
        else
        {
            inclined_to_quartile++;
        }
    }
    inclined_to_quartile = ((commentator_count<=4) ||(inclined_to_median<inclined_to_quartile)) ? 1:0;
    for(int i = commentator_count/2+1 ; i<commentator_count*3/4;i++)
    {
        int value = time_sum[i];
        if(value - median < third_quartile-value)
        {
            inclined_to_median++;
        }
        else
        {
            inclined_to_quartile++;
        } 
    }
    inclined_to_quartile = ((inclined_to_quartile) || (inclined_to_median<inclined_to_quartile)) ? 1:0;
    printf("Allowed ceil is %f \n",allowed_ceil);
    printf("Inclination is %d\n",inclined_to_quartile);

    if((third_quartile-first_quartile > allowed_ceil )|| (first_quartile-min >= (min)-lower_bound || max-third_quartile>=upper_bound-max) )
    {
        if (third_quartile-first_quartile > allowed_ceil)printf("That was not fair because third_quartile-first_quartile > allowed ceil\n");
        if(first_quartile-min >= (min)-lower_bound) printf("Minimum is close to lower edge\n");
        if(max-third_quartile>=upper_bound-max) printf("Maximum is close to upper edge\n");
        fairness=0;
    }

   
    printf("Mean : %f                               \n",mean);
    printf("Median is : %f                               \n",median);
    printf("Outlier number is : %d                               \n",outlier_count);
    printf("Minimum: Commentator %d with %f seconds \n",min_index,min);
    printf("Maximum: Commentator %d with %f seconds \n",max_index,max);
    printf("Total time spent : %s                   \n",timeStamp());
    printf("Total time spent on breaking news : %d                   \n",total_breaking_news);


    if(fairness ==0)
    {
        printf("That was not a fair program\n");
    }
    else
    {
        printf("That was a fair program\n");
    }
	printf("\n------------------------------------------");
}

int main(int argc, char *argv[])
{
    srand(time(NULL));
    char start_log[32];
    sprintf(start_log,"Program is starting!");
    discord_log(start_log);
    
    if(argc==11)
    {
        for(int i=1;i<11;i+=2)
        {
            char *param = argv[i];
            if(strcmp(param,"-p")==0)
            {
                probability = atof(argv[i+1])*100;
            }
            if(strcmp(param,"-q")==0)
            {
                question_count = atoi(argv[i+1]);
            }
            if(strcmp(param,"-n")==0)
            {
                commentator_count = atoi(argv[i+1]);
            }
            if(strcmp(param,"-t")==0)
            {
                time_bound = atof(argv[i+1]);
            }
            if(strcmp(param,"-b")==0)
            {
                breaking_probability = atof(argv[i+1])*100;
            }

        }
    }
    else
    {
        printf("Wrong input\n");
    }
    buffer = malloc(sizeof(int)*commentator_count);
    time_sum = malloc(sizeof(int)*commentator_count);
    time_as_string=malloc(sizeof(char)*15);

    /*Mutex,cond etc. initialize et*/
    pthread_cond_init(&questionAsked, NULL);
    pthread_cond_init(&ready_cond, NULL);
    pthread_cond_init(&your_turn, NULL);
    pthread_cond_init(&thinking_finished, NULL);
    pthread_cond_init(&commentator_finished, NULL);
    pthread_mutex_init(&question_mutex, NULL);
    pthread_cond_init(&breaking_news_handle, NULL);
    pthread_cond_init(&breaking_news_create, NULL);



    //Thread creation - 1 moderator - n commentator

    pthread_t *th = (pthread_t *)malloc(sizeof(pthread_t) * (commentator_count + 2));

    for (int i = 0; i < commentator_count + 2; i++)
    {
        if (i == commentator_count+1)
        {   
            if (pthread_create(&th[i], NULL, &moderator, NULL) != 0)
            {
                printf("Error creating moderator \n");
            }
        }
        else if(i==commentator_count)
        {
            if(pthread_create(&th[i],NULL,&breakingNews,NULL)!=0)
            {
                printf("Error creating breaking news handler\n");
            }
        }
        else
        {
            int *index = malloc(sizeof(int));
            *index=i;
            if (pthread_create(&th[i], NULL, &commentator, index) != 0)
            {
                printf("Error creating commentator \n");
            }
        }
    }

     while(1)
    {
        pthread_mutex_lock(&question_mutex);
        int prob = rand()%100 < breaking_probability;
        if(prob && currently_speaking)
        {
            pthread_cond_signal(&breaking_news_create);
        }
        
        if(questions_finished)
        {
            pthread_mutex_unlock(&question_mutex);
            break;
        }
        pthread_mutex_unlock(&question_mutex);
        sleep(1);
    }
    

    //Thread join
    for (int i = 0; i < commentator_count + 2; i++)
    {
        if (pthread_join(th[i], NULL) != 0)
        {
            printf("Error joining threads \n");
        }
    }

    pthread_cond_destroy(&questionAsked);
    pthread_cond_destroy(&ready_cond);
    pthread_cond_destroy(&thinking_finished);
    pthread_cond_destroy(&your_turn);
    pthread_cond_destroy(&breaking_news_handle);
    pthread_cond_destroy(&breaking_news_create);

    pthread_cond_destroy(&commentator_finished);
    pthread_mutex_destroy(&question_mutex);


      /*
        1- Change file descriptor from STD_OUT to a file
        2- Call statistics() function (statistics will be written into the file)
        3- In the parent, open this file, get the statistics
        4- Send this statistics to the discord webhook
    */

    pid_t my_pid = fork();
    if(my_pid ==0)
    {
        int execResult=open("execvResult.txt",O_WRONLY | O_CREAT,0777);
			if(execResult==-1) printf("Error opening file");
			dup2(execResult,STDOUT_FILENO);
            close(execResult);
            getStatistics();
    
    }
    else if(my_pid>0)
    {
        wait(NULL);
        char read_output[1024];	
        char notification_output[2056];
		FILE *fp=fopen("execvResult.txt","rt");
		if(fp==NULL) printf("Error opening file");

		while(fgets(read_output, sizeof(read_output), fp))
        {
            strcat(notification_output,read_output);
        }

        discord_log(notification_output+50);
        getStatistics();

    }
    else
    {
        printf("Error fork\n");
    }
    


    free(th);
    free(buffer);
    free(time_as_string);
    free(time_sum);
}