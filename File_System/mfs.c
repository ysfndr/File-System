// The MIT License (MIT)
// 
// Copyright (c) 2016, 2017 Trevor Bakker 
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

/*
Name: Yusuf Nadir Cavus
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>

#define WHITESPACE " \t\n"      // We want to split our command line up into tokens
                                // so we need to define what delimits our tokens.
                                // In this case  white space
                                // will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255     // The maximum command-line size
#define MAX_NUM_ARGUMENTS 10     // Mav shell only supports ten arguments
#define MAX_FILE_NAME 32
#define MAX_BLOCKS_PER_FILE 32
#define NUM_FILES 128
#define NUM_INODES 128

#define NUM_BLOCKS 4226
#define BLOCK_SIZE 8192

unsigned char data_blocks[NUM_BLOCKS][BLOCK_SIZE];
int used_blocks[NUM_BLOCKS];
char *open_file = NULL;

struct directory_entry {
  char *name;
  int valid;
  int inode_idx;
  int hidden;
  int read_only;
};

struct directory_entry *directory_ptr;

struct inode {
  time_t date;
  int valid;
  int size;
  int blocks[MAX_BLOCKS_PER_FILE];
};

struct inode *inode_array_ptr[NUM_INODES];

void init()
{
  directory_ptr = (struct directory_entry *) &data_blocks[0];

  for (int i = 0; i < NUM_FILES; i++)
  {
    directory_ptr[i].valid = 0; 
    directory_ptr[i].hidden = 0;
    directory_ptr[i].read_only = 0;
  }

  int inode_idx = 0;
  for (int i = 1; i < 130; i++)
  {
    inode_array_ptr[inode_idx++] = (struct inode *) &data_blocks[i];
  }

  for (int i = 0; i < 130; i++)
  {
    used_blocks[i] = 1;
  }
  for (int i = 130; i < NUM_BLOCKS; i++)
  {
    used_blocks[i] = 0;
  }

  for (int i = 0; i < NUM_INODES; i++)
  {
    for (int j = 0; j < MAX_BLOCKS_PER_FILE; j++)
    {
      inode_array_ptr[i]->blocks[j] = -1;
    }
  }

}

int df() 
{
  int count = 0;

  for (int i = 130; i < NUM_BLOCKS; i++)
  {
    if (used_blocks[i] == 0) count++;
  }

  return count * BLOCK_SIZE; 
}

int findFreeDirectoryEntry() 
{
  int ret = -1;

  for (int i = 0; i < NUM_FILES; i++)
  {
    if (directory_ptr[i].valid == 0)
    {
      ret = i;
      break;
    }
  }

  return ret;
}

int findFreeInode()
{
  int ret = -1;

  for (int i = 0; i < NUM_INODES; i++)
  {
    if (inode_array_ptr[i]->valid == 0)
    {
      ret = i;
      break;
    }
  }

  return ret;
}

int findFreeBlock() 
{
  int ret = -1;

  for (int i = 130; i < NUM_BLOCKS; i++)
  {
    if (used_blocks[i] == 0)
    {
      ret = i;
      break;
    }
  }

  return ret; 
}

int findFreeInodeBlockEntry (int inode_idx)
{
  int ret = -1;

  for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++)
  {
    if (inode_array_ptr[inode_idx]->blocks[i] == -1)
    {
      ret = i;
      break;
    }
  }

  return ret;
}

int find_file_dir_idx (char *filename)
{
  int ret = -1;

  for (int i = 0; i < NUM_FILES; i++)
  {
    if (directory_ptr[i].name != NULL)
    {
      if (!strcmp(directory_ptr[i].name, filename))
      {
        ret = i;
        break;
      }
    }
  }

  return ret;           
}

int find_first_block_index (int inode_idx)
{
  int ret = -1;

  for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++)
  {
    if (inode_array_ptr[inode_idx]->blocks[i] != -1)
    {
      ret = inode_array_ptr[inode_idx]->blocks[i];
      break;
    }
  }

  return ret;
}

int main()
{
  char * cmd_str = (char*) malloc( MAX_COMMAND_SIZE );

  init();

  while( 1 )
  {
    // Print out the mfs prompt
    printf ("mfs> ");

    // Read the command from the commandline.  The
    // maximum command that will be read is MAX_COMMAND_SIZE
    // This while command will wait here until the user
    // inputs something since fgets returns NULL when there
    // is no input
    while( !fgets (cmd_str, MAX_COMMAND_SIZE, stdin) );

    /* Parse input */
    char *token[MAX_NUM_ARGUMENTS];

    int   token_count = 0;                                 
                                                           
    // Pointer to point to the token
    // parsed by strsep
    char *arg_ptr;                                         
                                                           
    char *working_str  = strdup( cmd_str );                

    // we are going to move the working_str pointer so
    // keep track of its original value so we can deallocate
    // the correct amount at the end
    char *working_root = working_str;

    // Tokenize the input stringswith whitespace used as the delimiter
    while ( ( (arg_ptr = strsep(&working_str, WHITESPACE ) ) != NULL) && 
              (token_count<MAX_NUM_ARGUMENTS))
    {
      token[token_count] = strndup( arg_ptr, MAX_COMMAND_SIZE );
      if( strlen( token[token_count] ) == 0 )
      {
        token[token_count] = NULL;
      }
        token_count++;
    }

    if ( cmd_str[0] == '\n' ) 
    {
      continue;
    }

    if (!strcmp(token[0], "quit"))
    {
      exit(0);
    }

    /*PUT*/
    else if(!strcmp(token[0], "put"))
    {
      if (token[1] == NULL)
      {
        printf("Usage: put <filename>\n");
        continue;
      }

      int status;                   // Hold the status of all return values.
      struct stat buf;              // stat struct to hold the returns from the stat call

      status = stat( token[1], &buf ); 
      
      //Verify that the file exists
      if (status == -1)
      {
        printf("Unable to open file: %s\n", token[1] );
        perror("Opening the input file returned");
        continue;
      }

      //check the length of the file name.
      if (strlen(token[1]) > MAX_FILE_NAME)
      {
        printf("put error: File name too long\n");
        continue;
      }

      //Check if there is enough space
      if (buf.st_size > df())
      {
        printf("put error: Not enough disk space\n");
        continue;
      }

      int dir_idx = findFreeDirectoryEntry();

      if (dir_idx == -1)
      {
        printf("Error: Not enough disk space\n");
        continue;
      }

      directory_ptr[dir_idx].valid = 1; //used

      directory_ptr[dir_idx].name = (char *)malloc(strlen(token[1])); 
      strcpy(directory_ptr[dir_idx].name, token[1]); //Copy file name

      int inode_idx = findFreeInode();

      if (inode_idx == -1)
      {
        printf("Error: No free inodes\n");
        continue;
      }

      directory_ptr[dir_idx].inode_idx = inode_idx;
      
      inode_array_ptr[inode_idx]->valid = 1;
      inode_array_ptr[inode_idx]->size = buf.st_size;
      inode_array_ptr[inode_idx]->date = time(NULL); 

      // Open the input file read-only 
      FILE *ifp = fopen ( token[1], "r" ); 
      printf("Reading %d bytes from %s\n", (int) buf . st_size, token[1] );

      int copy_size = buf.st_size;
      int offset = 0;               
  
      // copy_size is initialized to the size of the input file so each loop iteration we
      // will copy BLOCK_SIZE bytes from the file then reduce our copy_size counter by
      // BLOCK_SIZE number of bytes. When copy_size is less than or equal to zero we know
      // we have copied all the data from the input file.
      while( copy_size > 0 )
      {
        int block_index = findFreeBlock();

        if (block_index == -1)
        {
          printf("Error: No free blocks\n");
          break;
        }

        used_blocks[block_index] = 1;

        int inode_block_entry = findFreeInodeBlockEntry(inode_idx);
        if (inode_block_entry == -1)
        {
          printf("Error: No free node blocks\n");
          break;
        }

        inode_array_ptr[inode_idx]->blocks[inode_block_entry] = block_index;

        // Index into the input file by offset number of bytes.  Initially offset is set to
        // zero so we copy BLOCK_SIZE number of bytes from the front of the file.  We 
        // then increase the offset by BLOCK_SIZE and continue the process.  This will
        // make us copy from offsets 0, BLOCK_SIZE, 2*BLOCK_SIZE, 3*BLOCK_SIZE, etc.
        fseek( ifp, offset, SEEK_SET );

        int num_bytes;

        // If the remaining number of bytes we need to copy is less than BLOCK_SIZE then
        // only copy the amount that remains. If we copied BLOCK_SIZE number of bytes we'd
        // end up with garbage at the end of the file.
        if( copy_size < BLOCK_SIZE )
        {
          num_bytes = copy_size;
        }
        else 
        {
          num_bytes = BLOCK_SIZE;
        }

        int bytes  = fread( data_blocks[block_index], num_bytes, 1, ifp );

        // If bytes == 0 and we haven't reached the end of the file then something is 
        // wrong. If 0 is returned and we also have the EOF flag set then that is OK.
        // It means we've reached the end of our input file.
        if( bytes == 0 && !feof( ifp ) )
        {
          printf("An error occured reading from the input file.\n");
          return -1;
        }

        clearerr( ifp );

        copy_size -= BLOCK_SIZE;
        offset += BLOCK_SIZE;
      }

      fclose( ifp );
    }
    
    /*GET*/
    else if(!strcmp(token[0], "get"))
    {
      if (token[1] == NULL)
      {
        printf("Usage: get <filename> or get <filename> <newfilename>\n");
        continue;
      }

      int status;                   
      struct stat buf;  

      //Check if a new file name is specified
      if (token[2] != NULL)
      {
        //check the length of the new file name.
        if (strlen(token[2]) > MAX_FILE_NAME)
        {
          printf("Error: New file name too long\n");
          continue;
        }
      }
      else 
      {
        token[2] = token[1]; //Use the old file name
      }

      int dir_idx = find_file_dir_idx(token[1]);

      if (dir_idx == -1 || directory_ptr[dir_idx].valid == 0)
      {
        printf("get Error: File not found\n");
        continue;
      }

      //Now, open the output file that we are going to write the data to.
      FILE *ofp;
      ofp = fopen(token[2], "w");

      status = stat( token[2], &buf );

      if( ofp == NULL || status == -1)
      {
        printf("Could not open output file: %s\n", token[2] );
        perror("Opening the output file returned");
        continue;
      }

      int inode_idx = directory_ptr[dir_idx].inode_idx;

      int copy_size = inode_array_ptr[inode_idx]->size;

      int block_index = find_first_block_index(inode_idx);

      int offset = 0;

      printf("Writing %d bytes to %s\n", copy_size, token[2] );

      while( copy_size > 0 )
      { 
        int num_bytes;

        if( copy_size < BLOCK_SIZE )
        {
          num_bytes = copy_size;
        }
        else 
        {
          num_bytes = BLOCK_SIZE;
        }

        fwrite( data_blocks[block_index], num_bytes, 1, ofp ); 

        copy_size -= BLOCK_SIZE;
        offset += BLOCK_SIZE;
        block_index++;

        fseek( ofp, offset, SEEK_SET );
      }

      fclose( ofp );
    }

    /*DEL*/
    else if(!strcmp(token[0], "del"))
    {
      if (token[1] == NULL)
      {
        printf("Usage: del <filename>\n");
        continue;
      }

      int dir_idx = find_file_dir_idx(token[1]);

      if (dir_idx == -1 || directory_ptr[dir_idx].valid == 0)
      {
        printf("del Error: File not found\n");
        continue;
      }

      //check if the file is read-only
      if (directory_ptr[dir_idx].read_only == 1)
      {
        printf("del: Cannot delete file because it is read-only\n");
        continue;
      }

      int inode_idx = directory_ptr[dir_idx].inode_idx;

      directory_ptr[dir_idx].valid = 0;
      inode_array_ptr[inode_idx]->valid = 0;

      //Before setting every block entries to -1,
      //get the first block index where the file starts.
      //Then, after setting the block values on the block array to -1,
      //store the block index that we just got in the first entry of block array
      //so we can undelete it after we delete.
      //the file will be permanently deleted when the inode is used again for a new file
      //since we did set the inode.valid to 0, and won't be able to
      //find the name of the file on this inode
      int undel_block_idx;

      for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++)
      {
        undel_block_idx = inode_array_ptr[inode_idx]->blocks[i];
        break;
      }

      for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++)
      {
        inode_array_ptr[inode_idx]->blocks[i] = -1;
      }

      inode_array_ptr[inode_idx]->blocks[0] = undel_block_idx;
    }

    /*UNDEL*/
    else if(!strcmp(token[0], "undel"))
    {
      if (token[1] == NULL)
      {
        printf("Usage: undel <filename>\n");
        continue;
      }

      int dir_idx = find_file_dir_idx(token[1]);

      if (dir_idx == -1)
      {
        printf("undel: Can not find the file\n");
        continue;
      }

      if (directory_ptr[dir_idx].valid == 1)
      {
        printf("The file you are trying to undelete has not been deleted\n");
      }

      directory_ptr[dir_idx].valid = 1;

      int inode_idx = directory_ptr[dir_idx].inode_idx;

      inode_array_ptr[inode_idx]->valid = 1;

      int block_index = inode_array_ptr[inode_idx]->blocks[0];

      int size = inode_array_ptr[inode_idx]->size;

      //similar to copying, but here we are just setting the 
      //block array values back
      int i = 0;
      while (size > 0)
      {
        inode_array_ptr[inode_idx]->blocks[i] = block_index;

        size -= BLOCK_SIZE;
        i++;
        block_index++;
      }
    }

    /*LIST*/
    else if(!strcmp(token[0], "list"))
    {
      int found = 0;

      for (int i = 0; i < NUM_FILES; i++)
      {
        if (directory_ptr[i].valid == 1 && directory_ptr[i].hidden == 0)
        {
          int inode_idx = directory_ptr[i].inode_idx;
          int size = inode_array_ptr[inode_idx]->size;
          char *name = directory_ptr[i].name;
          time_t time_added = inode_array_ptr[inode_idx]->date;
          char *time = strtok(ctime(&time_added), "\n");

          printf("%5d  %5s  %5s\n", size, time, name);

          found = 1;
        }
      }

      if (found == 0)
      {
        printf("list: No files found\n");
      }
    }

    /*DF*/
    else if(!strcmp(token[0], "df"))
    {
      printf("%d bytes free\n", df());
    }

    /*OPEN*/
    else if(!strcmp(token[0], "open"))
    {
      if (token[1] == NULL)
      {
        printf("Usage: open <filename>\n");
        continue;
      }

      int status;                   // Hold the status of all return values.
      struct stat buf;              // stat struct to hold the returns from the stat call

      status = stat( token[1], &buf ); 
      
      //Verify that the file exists
      if (status == -1)
      {
        printf("open: File not found\n");
        continue;
      }

      //check the length of the file name.
      if (strlen(token[1]) > MAX_FILE_NAME)
      {
        printf("put error: File name too long\n");
        continue;
      }

      //store file name while it is open
      open_file = (char *)malloc(strlen(token[1]));
      strncpy(open_file, token[1], strlen(token[1]));
    }

    /*SAVE*/
    else if(!strcmp(token[0], "save"))
    {
      //check if a file is open
      if (open_file == NULL)
      {
        printf("save: There is no open file\n");
        continue;
      }

      /*Execute put using the open file's name*/

      int status;                   
      struct stat buf;              

      status = stat( open_file, &buf ); 
      
      //Verify that the file exists
      if (status == -1)
      {
        printf("Unable to open file: %s\n", open_file );
        perror("Opening the input file returned");
        continue;
      }

      //check the length of the file name.
      if (strlen(open_file) > MAX_FILE_NAME)
      {
        printf("put error: File name too long\n");
        continue;
      }

      //Check if there is enough space
      if (buf.st_size > df())
      {
        printf("put error: Not enough disk space\n");
        continue;
      }

      int dir_idx = findFreeDirectoryEntry();

      if (dir_idx == -1)
      {
        printf("Error: Not enough disk space\n");
        continue;
      }

      directory_ptr[dir_idx].valid = 1; //used

      directory_ptr[dir_idx].name = (char *)malloc(strlen(open_file)); 
      strncpy(directory_ptr[dir_idx].name, open_file, strlen(open_file)); //Copy file name

      int inode_idx = findFreeInode();

      if (inode_idx == -1)
      {
        printf("Error: No free inodes\n");
        continue;
      }

      directory_ptr[dir_idx].inode_idx = inode_idx;
      
      inode_array_ptr[inode_idx]->valid = 1;
      inode_array_ptr[inode_idx]->size = buf.st_size;
      inode_array_ptr[inode_idx]->date = time(NULL); 

      // Open the input file read-only 
      FILE *ifp = fopen ( open_file, "r" ); 
      printf("Reading %d bytes from %s\n", (int) buf . st_size, open_file );

      int copy_size = buf.st_size;
      int offset = 0;               
  
      // copy_size is initialized to the size of the input file so each loop iteration we
      // will copy BLOCK_SIZE bytes from the file then reduce our copy_size counter by
      // BLOCK_SIZE number of bytes. When copy_size is less than or equal to zero we know
      // we have copied all the data from the input file.
      while( copy_size > 0 )
      {
        int block_index = findFreeBlock();

        if (block_index == -1)
        {
          printf("Error: No free blocks\n");
          break;
        }

        used_blocks[block_index] = 1;

        int inode_block_entry = findFreeInodeBlockEntry(inode_idx);
        if (inode_block_entry == -1)
        {
          printf("Error: No free node blocks\n");
          break;
        }

        inode_array_ptr[inode_idx]->blocks[inode_block_entry] = block_index;

        // Index into the input file by offset number of bytes.  Initially offset is set to
        // zero so we copy BLOCK_SIZE number of bytes from the front of the file.  We 
        // then increase the offset by BLOCK_SIZE and continue the process.  This will
        // make us copy from offsets 0, BLOCK_SIZE, 2*BLOCK_SIZE, 3*BLOCK_SIZE, etc.
        fseek( ifp, offset, SEEK_SET );

        int num_bytes;

        // If the remaining number of bytes we need to copy is less than BLOCK_SIZE then
        // only copy the amount that remains. If we copied BLOCK_SIZE number of bytes we'd
        // end up with garbage at the end of the file.
        if( copy_size < BLOCK_SIZE )
        {
          num_bytes = copy_size;
        }
        else 
        {
          num_bytes = BLOCK_SIZE;
        }

        int bytes  = fread( data_blocks[block_index], num_bytes, 1, ifp );

        // If bytes == 0 and we haven't reached the end of the file then something is 
        // wrong. If 0 is returned and we also have the EOF flag set then that is OK.
        // It means we've reached the end of our input file.
        if( bytes == 0 && !feof( ifp ) )
        {
          printf("An error occured reading from the input file.\n");
          return -1;
        }

        clearerr( ifp );

        copy_size -= BLOCK_SIZE;
        offset += BLOCK_SIZE;
      }

      fclose( ifp );

    }

    /*CLOSE*/
    else if(!strcmp(token[0], "close"))
    {
      //check if any file is open
      if (open_file == NULL)
      {
        printf("close: There is no open file\n");
        continue;
      }

      //free and set open_file to NULL
      free(open_file);
      open_file = NULL;
    }

    /*ATTRIB*/
    else if(!strcmp(token[0], "attrib"))
    {
      if (token[1] == NULL || token[2] == NULL)
      {
        printf("Usage: attrib +/-<attribute> <filename>\n");
        continue;
      }

      int dir_idx = find_file_dir_idx(token[2]);

      if (dir_idx == -1 || directory_ptr[dir_idx].valid == 0)
      {
        printf("attrib: File not found\n");
        continue;
      }

      if (!strcmp(token[1], "+h") && directory_ptr[dir_idx].hidden == 0)
      {
        directory_ptr[dir_idx].hidden = 1;
      }
      else if (!strcmp(token[1], "-h") && directory_ptr[dir_idx].hidden == 1)
      {
        directory_ptr[dir_idx].hidden = 0;
      }
      else if (!strcmp(token[1], "+r") && directory_ptr[dir_idx].read_only == 0)
      {
        directory_ptr[dir_idx].read_only = 1;
      }
      else if (!strcmp(token[1], "-r") && directory_ptr[dir_idx].read_only == 1)
      {
        directory_ptr[dir_idx].read_only = 0;
      }
    }

    /*CREATEFS*/
    else if(!strcmp(token[0], "createfs"))
    {
      // Not yet implemented
      continue;
    }
  }

  //free directory_ptr.name
  for (int i = 0; i < NUM_FILES; i++)
  {
    if (directory_ptr[i].name)
    {
      free(directory_ptr[i].name);
      directory_ptr[i].name = NULL;
    }
  }

  return 0;
}
