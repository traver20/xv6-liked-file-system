#include<iostream>
#include<io.h>
#include<cstdio>
#include"types.h"
#include<algorithm>
#include"defs.h"
#include"sh.h"
#include<Windows.h>
#include<tchar.h>
#include<unordered_map>
#include<fstream>
#include<string>
#include<unordered_set>

using namespace std;

FILE* disk;
FILE* host;
//disk layout:
//[superblock | free bit map | inode blocks | root directory | data blocks]

const uint BSIZE = 1024;
const int blocksPerWord = 32;
const int shift = 5;
const int mask = 0x1F;
const int nBlocks = 1024 * 100;
int bitmap[nBlocks / blocksPerWord];

//share_memory
HANDLE mapShare;
char* pBegin;
HANDLE server;
HANDLE user, write_mem, fileW;
char IPC_buf[BSIZE];

const uint NINODEBLOCK = 4;
const uint NDIRECT = 6;

const uint NINODE = 20;//maximum number of active inodes

const uint T_DIR = 1;
const uint T_FILE = 2;

const int FSMAGIC = 0x10203040;

void share_mem_write(const char* s)
{
	ResetEvent(write_mem);
	strcat(IPC_buf, s);
	SetEvent(write_mem);
}

class userData
{
public:
	HANDLE canRead, hasWriten;
	bool readable, writeable;
	userData()
	{

	}
	userData(string name)
	{
		string writen = name + "writen";
		readable = writeable = true;
		//HANDLE th = CreateEvent(NULL, TRUE, FALSE, (LPCWSTR)read.c_str());
		canRead= CreateEvent(NULL, TRUE, TRUE, (LPCWSTR)name.c_str());
		hasWriten = CreateEvent(NULL, TRUE, FALSE, (LPCWSTR)writen.c_str());
	}
	~userData()
	{
		//CloseHandle(canRead);
		//CloseHandle(hasWriten);
	}
};

unordered_map<string, userData> users;

void make_userData()
{
	fstream f;
	f.open("users.txt", ios::in);
	if (!f.is_open())
		cout << "open failed" << endl;
	while (!f.eof())
	{
		string line;
		getline(f, line);
		users.insert({ line,userData(line) });
	}
	f.close();
}

void login(const char* cname)
{
	string name(cname);
	if (users.find(name) == users.end())
	{
		share_mem_write("no such user!");
	}
	else
	{
		share_mem_write("ok!");
	}
}

struct dinode
{
	uint type;
	uint size;
	uint addrs[NDIRECT];//32 bytes per dinode
};

struct superblock
{
	uint size;
	uint nblocks; //data blocks
	uint ninodes; //number of inodes
	uint inodestart; //first inode block
	uint bmapstart; //first map block
};

struct inode
{
	uint ref;
	uint inum;
	uint  type;
	uint size;
	uint addrs[NDIRECT];//data blocks address
};

struct inode itable[NINODE];

struct inode* cur_dir;
struct inode cur_file;

const uint IPB = (BSIZE / sizeof(dinode));//Inodes per block

uint IBLOCK(uint i, const superblock& sb)
{
	return ((i) / IPB + sb.inodestart);//Block contaning inode i
}

const uint BPB = (BSIZE * 8);//Bitmap bits per block

uint BBLOCK(uint b, const superblock& sb)
{
	return ((b) / BPB + sb.bmapstart);
}

const uint DIRSIZ = 14;

struct dir_entry
{
	ushort inum;
	char name[DIRSIZ];//16 bytes per entry
};

struct superblock sb;

/*void*
memmove(void* dst, const void* src, uint n)
{
	const char* s;
	char *d;
	if (n == 0)
		return dst;
	s = (char*)src;
	d = (char*)dst;
	if (s<d && s + n>d)
	{
		s += n;
		d += n;
		while (n-- > 0)
			*--d = *--s;
	}
	else
	{
		while (n-- > 0)
			*d++ = *s++;
	}
	return dst;
*/

struct buf
{
	uint blockno;//block number
	uchar data[BSIZE];//data
};

struct buf buf;
char copy_buf[BSIZE];

struct inode* namei(char*);
void bread(uint);
void brelease();

void info_display()
{
	char size[20], nblocks[20], bmapstart[20], inodestart[20], ninodes[20];
	_itoa(sb.size, size, 10);
	_itoa(sb.nblocks, nblocks, 10);
	_itoa(sb.bmapstart, bmapstart, 10);
	_itoa(sb.inodestart, inodestart, 10);
	_itoa(sb.ninodes, ninodes, 10);
	share_mem_write("size:  ");
	share_mem_write(size);
	share_mem_write("\nnumber of data blocks:  ");
	share_mem_write(nblocks);
	share_mem_write("\nbit map starts at block ");
	share_mem_write(bmapstart);
	share_mem_write("\ninodes start at block ");
	share_mem_write(inodestart);
	share_mem_write("\nnumber of inodes: ");
	share_mem_write(ninodes);
	share_mem_write("\n");
}

void mapping_display()
{
	uint b,bi,m;
	for (b = 0; b < sb.size; b += BPB)
	{
		bread(BBLOCK(b, sb));
		for (bi = 0; bi < BPB; ++bi)
		{
			m = 1 << bi % 8;
			if ((buf.data[bi / 8] & m) != 0)
				cout << "block " << b + bi << " has been used" << endl;
		}
		brelease();
	}
}

void inode_display(struct inode* ip)
{
	cout << "size: " << ip->size<<endl;
	cout << "inum: " << ip->inum << endl;
	cout << "type: " << ip->type << endl;
	cout << "addrs:" << endl;
	for (uint i = 0; i < NDIRECT; ++i)
		if (ip->addrs[i] != 0)
			cout << "block " << ip->addrs[i] << endl;
}

void bread(uint blockno)
{
	buf.blockno = blockno;
	fseek(disk, blockno*BSIZE, SEEK_SET);
	fread(buf.data, 1, BSIZE, disk);
}//read a block from disk

void brelease()
{
	fseek(disk, buf.blockno*BSIZE, SEEK_SET);
	fwrite(buf.data, 1, BSIZE, disk);
}//updata disk when done

void fsinit()
{
	//initialize the superblock
	bread(0);
	memmove(&sb, buf.data, sizeof(sb));
}

void bzero(uint bno)
{
	bread(bno);
	memset(buf.data, 0, BSIZE);
	brelease();
}//zeros a block

uint balloc()
{
	uint b, bi, m;
	for (b = 0; b < sb.size; b += BPB)
	{
		bread(BBLOCK(b, sb));
		for (bi = 0; bi < BPB && bi + b < sb.size; ++bi)
		{
			m = 1 << (bi % 8);
			if ((buf.data[bi / 8] & m) == 0)
			{
				buf.data[bi / 8] |= m;
				brelease();
				bzero(b + bi);
				return b + bi;
			}
		}
		brelease();
	}
	printf("out of blocks\n");
	return -1;
}//alloc a free block

void bfree(uint b)
{
	uint bi, m;
	bread(BBLOCK(b, sb));
	bi = b % BPB;
	m = 1 << (bi % 8);
	if ((buf.data[bi / 8] & m) == 0)
	{
		printf("freeing free block");
	}
	buf.data[bi / 8] &= ~m;
	brelease();
}

uint bmap(struct inode* ip, uint bn)
{
	uint addr;
	if (bn < NDIRECT)
	{
		if ((addr = ip->addrs[bn]) == 0)
			ip->addrs[bn] = addr = balloc();
		return addr;
	}
	printf("bmap:out of range\n");
	return -1;
}

struct inode* iget(uint inum)
{
	struct inode* ip, * empty = 0;

	for (ip = &itable[0]; ip < &itable[NINODE]; ip++)
	{
		if (ip->ref > 0 && ip->inum == inum)
		{
			ip->ref++;
			return ip;
		}
		if (empty == 0 && ip->ref == 0)
			empty = ip;
	}
	if (empty == 0)
	{
		printf("iget: no inodes\n");
		return 0;
	}

	ip = empty;
	ip->inum = inum;
	ip->ref = 1;

	struct dinode* dip;
	bread(IBLOCK(ip->inum, sb));
	dip = (struct dinode*)buf.data + ip->inum % IPB;
	ip->type = dip->type;
	ip->size = dip->size;
	memmove(ip->addrs, dip->addrs, sizeof(dip->addrs));
	brelease();
	return ip;
}

struct inode* ialloc(uint type)
{
	uint inum;
	struct dinode* dip;

	for (inum = 1; inum < sb.ninodes; ++inum)
	{
		bread(IBLOCK(inum, sb));
		dip = (struct dinode*)buf.data + inum % IPB;
		if (dip->type == 0)
		{
			memset(dip, 0, sizeof(*dip));
			dip->type = type;
			brelease();
			return iget(inum);
		}
	}
	printf("ialloc: no inodes\n");
	return 0;
}

//update a dinode when changed
void iupdate(struct inode* ip)
{
	struct dinode* dip;
	bread(IBLOCK(ip->inum, sb));
	dip = (struct dinode*)buf.data + ip->inum % IPB;
	dip->type = ip->type;
	dip->size = ip->size;
	memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
	brelease();
}

struct inode* idup(struct inode* ip)
{
	ip->ref++;
	return ip;
}


void itrunc(struct inode* ip)
{
	uint i;
	for (i = 0; i < NDIRECT; ++i)
	{
		if (ip->addrs[i])
		{
			bfree(ip->addrs[i]);
			ip->addrs[i] = 0;
		}
	}
	ip->size = 0;
	iupdate(ip);
}

void iput(struct inode* ip)
{
	/*if (ip->ref == 1)
	{
		itrunc(ip);
		ip->type = 0;
		iupdate(ip);
	}
	ip->ref--;*/
	if (ip->ref > 0)
	{
		ip->ref--;
		if (ip->ref == 0)
			iupdate(ip);
	}
}

uint readi(struct inode* ip, uint64 dst, uint off, uint n)
{
	uint tot, m;
	if (off > ip->size || off + n < off)
	{
		printf("read i failed\n");
		return 0;
	}
	if (off + n > ip->size)
		n = ip->size - off;

	for (tot = 0; tot < n; tot += m, off += m, dst += m)
	{
		bread(bmap(ip, off / BSIZE));
		m = min(n - tot, BSIZE - off % BSIZE);
		memmove((char*)dst, buf.data + (off % BSIZE), m);
		brelease();
	}
	return tot;
}

uint writei(struct inode* ip, uint64 src, uint off, uint n)
{
	uint tot, m;
	if (off > ip->size || off + n < off)
		return -1;
	if (off + n > NDIRECT* BSIZE)
		return -1;
	for (tot = 0; tot < n; tot += m, off += m, src += m)
	{
		bread(bmap(ip, off / BSIZE));
		m = min(n - tot, BSIZE - off % BSIZE);
		memmove(buf.data + (off % BSIZE), (char*)src, m);
		brelease();
	}
	if (off > ip->size)
		ip->size = off;
	iupdate(ip);

	return tot;
}
/*
uint strncmp(const char* p, const char* q, uint n)
{
	while (n > 0 && *p && *p == *q)
	{
		n--;
		p++;
		q++;
	}
	if (n == 0)
		return 0;
	return (uchar)*p - (uchar)*q;
}

char* strncpy(char* s, const char* t, uint n)
{
	char* os;
	os = s;
	while (n-- > 0 && (*s++ = *t++) != 0)
		;
	while (n-- > 0)
		*s++ = 0;
	return os;
}*/

struct inode* dir_look(struct inode* dp,const char* name, uint* poff)
{
	uint off, inum;
	struct dir_entry de;
	if (dp->type != T_DIR)
	{
		printf("dir_look not directory\n");
		return 0;
	}
	for (off = 0; off < dp->size; off += sizeof(de))
	{
		if (readi(dp, (uint64)&de, off, sizeof(de)) != sizeof(de))
		{
			printf("dir look failed\n");
			return 0;
		}
		if (de.inum == 0)
			continue;
		if (strcmp(de.name, name) == 0)
		{
			if (poff)
				*poff = off;
			inum = de.inum;
			return  iget(inum);
		}
	}
	return 0;
}

uint dir_link(struct inode* dp,const char* name, uint inum)
{
	uint off;
	struct dir_entry de;
	struct inode* ip;
	if ((ip = dir_look(dp, name, 0)) != 0)
	{
		iput(ip);
		printf("%s already exists\n", name);
		return -1;
	}
	for (off = 0; off < dp->size; off += sizeof(de))
	{
		if (readi(dp, (uint64)&de, off, sizeof(de)) != sizeof(de))
		{
			printf("dir_link readi failed\n");
			return -1;
		}
		if (de.inum == 0)
			break;
	}
	strncpy(de.name, name, DIRSIZ);
	de.inum = inum;
	if (writei(dp, (uint64)&de, off, sizeof(de)) != sizeof(de))
	{
		printf("dir_link write failed\n");
		return -1;
	}
	return 0;
}

char* skipelem(char* path, char* name)
{
	char* s;
	uint len;
	while (*path == '/')
		++path;
	if (*path == 0)
		return 0;
	s = path;
	while (*path != '/' && *path != 0)
		++path;
	len = path - s;
	if (len > DIRSIZ)
		memmove(name, s, DIRSIZ);
	else
	{
		memmove(name, s, len);
		name[len] = 0;
	}
	while (*path == '/')
		++path;
	return path;
}

struct inode* namex(char* path, int nameiparent, char* name)
{
	struct inode* ip, * next;
	if (*path == '/')
		ip = iget(1);
	else
		ip = idup(cur_dir);
	while ((path = skipelem(path, name)) != 0)
	{
		if (ip->type != T_DIR)
		{
			iput(ip);
			return 0;
		}
		if (nameiparent && *path == '\0')
		{
			return ip;
		}
		if ((next = dir_look(ip, name, 0)) == 0)
		{
			iput(ip);
			return 0;
		}
		iput(ip);
		ip = next;
	}
	if (nameiparent)
	{
		printf("namex: put ip\n");
		iput(ip);
		return 0;
	}
	return ip;
}

struct inode* namei(char* path)
{
	char name[DIRSIZ];
	return namex(path, 0, name);
}

struct inode* nameiparent(char* path, char* name)
{
	return namex(path, 1, name);
}

void make_ls_format(uint lev)
{
	for (uint i = 0; i < lev; ++i)
		share_mem_write("---- ");
	share_mem_write("\n");
}

void ls(struct inode* ip)
{
	uint off;
	struct dir_entry de;
	struct inode* cp;
	share_mem_write("name\t\t\tinode\t\t\ttype\n");
	for (off = 0; off < ip->size; off += sizeof(de))
	{
		if (readi(ip, (uint64)&de, off, sizeof(de)) != sizeof(de))
		{
			printf("ls readi\n");
			return;
		}
		if (de.inum == 0)
			continue;
		cp=iget(de.inum);
		share_mem_write(de.name);
		share_mem_write("\t\t\t");
		char inum[20];
		_itoa(de.inum, inum, 10);
		share_mem_write(inum);
		share_mem_write("\t\t\t");
		if (cp->type == T_DIR)
			share_mem_write("DIR\n");
		else
			share_mem_write("FILE\n");
		iput(cp);
	}
}

void ls_recur(uint lev,struct inode* ip)
{
	uint off;
	struct dir_entry de;
	struct inode* cp;
	make_ls_format(lev);
	for (off = 0; off < ip->size; off += sizeof(de))
	{
		if (readi(ip, (uint64)&de, off, sizeof(de)) != sizeof(de))
		{
			printf("ls readi\n");
			return;
		}
		if (de.inum == 0)
			continue;
		cp = iget(de.inum);
		share_mem_write(de.name);
		share_mem_write("\t\t\t");
		char str[10] = { 0 };
		_itoa(de.inum, str, 10);
		share_mem_write(str);
		share_mem_write("\t\t\t");
		//printf("%s\t\t\t%d\t\t\t", de.name, de.inum);
		if (cp->type == T_DIR)
		{
			share_mem_write("DIR\n");
			if (strcmp(de.name, ".") != 0 && strcmp(de.name, "..") != 0)
				ls_recur(lev + 1, cp);
		}
		else
			share_mem_write("FILE\n");
		iput(cp);
	}
	make_ls_format(lev);
}

struct inode* create(const char* tpath, uint type)
{
	char path[20];
	strcpy(path, tpath);
	struct inode* ip, * dp;
	char name[DIRSIZ];

	if ((dp = nameiparent(path, name)) == 0)
	{
		return 0;
	}

	if ((ip = dir_look(dp, name, 0)) != 0)
	{
		iput(dp);
		if (type == ip->type)
			return ip;
		iput(ip);
		return 0;
	}

	if ((ip = ialloc(type)) == 0)
	{
		printf("create: ialloc\n");
		return 0;
	}

	if (type == T_DIR)
	{
		if (dir_link(ip, ".", ip->inum) < 0 || dir_link(ip, "..", dp->inum) < 0)
		{
			printf("failed create dots\n");
			return 0;
		}
	}

	if (dir_link(dp, name, ip->inum) < 0)
	{
		printf("create: dir_link\n");
		return 0;
	}

	iput(dp);
	return ip;
}

uint ch_dir(const char* tpath)
{
	char path[20];
	strcpy(path, tpath);
	struct inode* ip;
	if ((ip = namei(path)) == 0)
	{
		printf("ch_dir: namei \n");
		return -1;
	}
	if (ip->type != T_DIR)
	{
		iput(ip);
		return -1;
	}
	iput(cur_dir);
	cur_dir = ip;
	return 0;
}

void copy(char* src, const char* dst)
{
	struct inode* ip_dst, *ip_src;
	ip_dst = create(dst, T_FILE);
	if (ip_dst == NULL)
	{
		printf("copy:create\n");
		return;
	}
	ip_src = namei(src);
	uint sz = ip_src->size;
	uint off = 0;
	while (sz > BSIZE)
	{
		readi(ip_src, (uint64)copy_buf, off, 1024);
		sz -= 1024;
	}
	readi(ip_src, (uint64)copy_buf, off, sz);
	writei(ip_dst, (uint64)copy_buf, 0, ip_src->size);
}

void copy_host(const char* host_path, const char* fs_path)
{
	struct inode* ip;
	ip=create(fs_path, T_FILE);
	if (ip == NULL)
	{
		printf("copy:create\n");
		return;
	}
	if ((host = fopen(host_path, "r")) == NULL)
	{
		printf("copy:fopen\n");
		return;
	}
	uint off = 0;
	uint cnt;
	while ((cnt = fread(copy_buf, 1, 1024, host)) != 0)
	{
		writei(ip, (uint64)copy_buf, 0, cnt);
		off += cnt;
	}
	fclose(host);
}

void cat(struct inode* ip)
{
	uint n = ip->size;
	char data[BSIZE];
	uint tot = 0;
	while (n > 1024)
	{
		tot += readi(ip, (uint64)data, tot, 1024);
		printf("%s", data);
		n -= 1024;
	}
	tot += readi(ip, (uint64)data, tot, n);
	//printf("size:%d tot:%d\n", n, tot);
	if(n<1024)
		data[n] = '\0';
	share_mem_write(data);
	share_mem_write("\n");
}

void del_recur(struct inode* ip)
{
	if (ip->type == T_DIR)
	{
		uint off;
		struct dir_entry de;
		for (off = 0; off < ip->size; off += sizeof(de))
		{
			if (readi(ip, (uint64)&de, off, sizeof(de)) != sizeof(de))
			{
				printf("del_recur:readi\n");
				return;
			}
			if (de.inum == 0 || strncmp(".",de.name, DIRSIZ) == 0 || strncmp("..",de.name, DIRSIZ) == 0)
				continue;
			del_recur(iget(de.inum));
		}
	}
	itrunc(ip);
	ip->type = 0;
	iupdate(ip);
	iput(ip);
}

void del_file(char* path)
{
	char name[DIRSIZ];
	struct inode* dp = nameiparent(path, name);
	if (dp == NULL)
	{
		printf("del:nameiparent\n");
		return;
	}
	uint off = 0;
	struct inode* ip = dir_look(dp, name, &off);
	if (ip == NULL)
	{
		iput(dp);
		printf("no such file\n");
		return;
	}
	inode_display(ip);
	itrunc(ip);
	ip->type = 0;
	iupdate(ip);
	iput(ip);

	//zeros a directory entry
	struct dir_entry reset;
	memset(&reset, 0, sizeof(reset));
	writei(dp, (uint64)&reset, off, sizeof(reset));
	iput(dp);
}

void del_dir(char* path)
{
	struct inode* ip,* dp;
	char name[DIRSIZ];
	dp = nameiparent(path, name);
	if (dp == NULL)
	{
		printf("del:nameiparent\n");
		return;
	}
	uint off = 0;
	ip = dir_look(dp, name, &off);
	if (ip == NULL)
	{
		iput(dp);
		printf("no such directory\n");
		return;
	}
	if (ip->size != 0)
	{
		printf("deleting non empty directory:[Y/N]\n");
	}
	del_recur(ip);
	struct dir_entry reset;
	memset(&reset, 0, sizeof(reset));
	writei(dp, (uint64)&reset, off, sizeof(reset));
	iput(dp);
}

void write_file(char* path)
{
	ResetEvent(fileW);
	struct inode* ip = namei(path);
	if (ip == NULL)
	{
		printf("path not exist\n");
		return;
	}
	if (ip->type == T_DIR)
	{
		printf("cannot write a directory\n");
		return;
	}
	strcpy(pBegin, "content:");
	SetEvent(user);
	ResetEvent(server);
	WaitForSingleObject(server, INFINITE);
	char content[100];
	strcpy_s(content, 100, pBegin+20);
	if (writei(ip, (uint64)content, 0, strlen(content)+1) == -1)
	{
		printf("write_file:writei\n");
	}
}

void disk_init()
{
	if ((disk = fopen("disk", "r+")) == NULL)
	{
		disk = fopen("disk", "w+");
		cout << "size:" << _filelength(_fileno(disk)) << endl;
		if (_chsize(_fileno(disk), 100 * 1024 * 1024) != 0)
			cout << "file size change failed" << endl;
		cout << "size now:" << _filelength(_fileno(disk)) << endl;
		cout << "create disk" << endl;
	}
	else
	{
		cout << "created before"<<endl;
	}
}

void disk_close()
{
	fclose(disk);
}

void make_super()
{
	sb.size = 1024 * 100;
	sb.nblocks = 1024 * 100 - 18;
	sb.bmapstart = 1;
	sb.inodestart = 14;
	sb.ninodes = 128;
	fseek(disk, 0, SEEK_SET);
	fwrite(&sb, sizeof(sb), 1, disk);
}

void make_mapping()
{
	bzero(sb.bmapstart);
	bread(sb.bmapstart);
	cout << "bmap start: " << sb.bmapstart << endl;
	uint bi,m;
	for (bi = 0; bi < 18; ++bi)
	{
		m = 1 << bi % 8;
		buf.data[bi / 8] |= m;
		cout <<bi/8<<"  "<< (uint)buf.data[bi/8] << endl;
	}
	brelease();
}

void make_rootdata(const char* tname)
{
	char name[DIRSIZ];
	strncpy(name, tname, DIRSIZ);
	struct inode* ip;
	ip = ialloc(T_FILE);
	inode_display(ip);
	if (dir_link(cur_dir, name, ip->inum) < 0)
	{
		printf("make_root: dir_link\n");
	}
}

void make_root()
{
	cur_dir = ialloc(T_DIR);
	//inode_display(cur_dir);
	make_rootdata("hello world");
	//inode_display(cur_dir);
	ls(cur_dir);
}

void make_fs()
{
	make_super();
	info_display();
	make_mapping();
	mapping_display();
	make_root();
}//create file system

enum command
{
	INFO, CD, LS, LSS, MKDIR, RD, NEWFILE, CAT, COPY, DEL,WRITE, LOGIN, EXIT, ERR
};
command getCommand(const char* s)
{
	if (strncmp("exit", s, 4) == 0)
		return EXIT;
	if (strncmp("ls /s", s, 5) == 0)
		return LSS;
	if (strncmp("info", s, 4) == 0)
		return INFO;
	if (strncmp("login", s, 5) == 0)
		return LOGIN;
	if (strncmp("cd", s, 2) == 0)
		return CD;
	if (strncmp("ls", s, 2) == 0)
		return LS;
	if (strncmp("mkdir", s, 5) == 0)
		return MKDIR;
	if (strncmp("rd", s, 2) == 0)
		return RD;
	if (strncmp("newfile", s, 7) == 0)
		return NEWFILE;
	if (strncmp("write", s, 5) == 0)
		return WRITE;
	if (strncmp("cat", s, 3) == 0)
		return CAT;
	if (strncmp("copy", s, 4) == 0)
		return COPY;
	if (strncmp("del", s, 3) == 0)
		return DEL;
	return ERR;
}

void make_path(char* paths, char* src, char* dst)
{
	char* p = paths;
	while (*p != ' ')
		p++;
	uint len = p - paths;
	printf("len:%d\n", len);
	strncpy(src, paths, len);
	src[len] = '\0';
	paths = ++p;
	while (*p != '\0')
		p++;
	len = p - paths;
	printf("len:%d\n", len);
	strncpy(dst, paths, len);
	dst[len] = '\0';
	printf("src:%s\ndst:%s\n", src, dst);
}

void shell()
{
	char buf[100];
	while (true)
	{
		printf("$ ");
		IPC_buf[0] = '\0';
		fgets(buf, 100, stdin);
		buf[strlen(buf) - 1] = '\0';
		char cmd[64];
		strcpy_s(cmd, 64, buf);
		command cur = getCommand(cmd);
		switch (cur)
		{
		case INFO:
			printf("info\n");
			info_display();
			mapping_display();
			break;
		case CD:
			printf("cd\n");
			ch_dir(cmd + 3);
			break;
		case LS:
			share_mem_write("ls\n");
			ls(cur_dir);
			break;
		case LSS:
			ls_recur(1, cur_dir);
			break;
		case MKDIR:
			printf("mkdir\n");
			create(cmd + 6, T_DIR);
			break;
		case RD:
			printf("rd\n");
			del_dir(cmd + 3);
			break;
		case NEWFILE:
			printf("newfile\n");
			create(cmd + 8, T_FILE);
			break;
		case WRITE:
			printf("write\n");
			write_file(cmd + 6);
			break;
		case CAT:
			share_mem_write("cat\n");
			cat(namei(cmd + 4));
			break;
		case COPY:
			printf("copy\n");
			char src[32];
			char dst[32];
			if (strncmp(cmd + 6, "<host>", 6) == 0)
			{
				make_path(cmd + 12, src, dst);
				copy_host(src, dst);
			}
			else
			{
				make_path(cmd + 5, src, dst);
				copy(src, dst);
			}
			break;
		case DEL:
			printf("del\n");
			del_file(cmd + 4);
			break;
		case EXIT:
			printf("exit\n");
			break;
		case ERR:
			printf("command not exists\n");
			break;
		default:
			break;
		}
		printf("buf data:\n%s\n", IPC_buf);
		if (cur == EXIT)
			break;
	}
}

void shell_process()
{
	char cmd[64];
	strcpy_s(cmd, 64, IPC_buf+20);
	cout << "command: " << cmd << endl;
	IPC_buf[0] = '\0';
	command cur = getCommand(cmd);
	switch (cur)
	{
	case INFO:
		share_mem_write("info\n");
		info_display();
		mapping_display();
		break;
	case LOGIN:
		login(cmd+6);
		break;
	case CD:
		share_mem_write("cd\n");
		if (strlen(cmd) < 4)
		{
			share_mem_write("You need to provide a path\n");
		}
		else
		{
			ch_dir(cmd + 3);
		}
		break;
	case LS:
		share_mem_write("ls\n");
		ls(cur_dir);
		break;
	case LSS:
		share_mem_write("ls /s\n");
		ls_recur(1, cur_dir);
		break;
	case MKDIR:
		share_mem_write("mkdir\n");
		create(cmd + 6, T_DIR);
		break;
	case RD:
		share_mem_write("rd\n");
		del_dir(cmd + 3);
		break;
	case NEWFILE:
		share_mem_write("newfile\n");
		create(cmd + 8, T_FILE);
		break;
	case WRITE:
		share_mem_write("write\n");
		write_file(cmd + 6);
		break;
	case CAT:
		share_mem_write("cat\n");
		cat(namei(cmd + 4));
		break;
	case COPY:
		share_mem_write("copy\n");
		char src[32];
		char dst[32];
		if (strncmp(cmd + 5, "<host>", 6) == 0)
		{
			make_path(cmd + 12, src, dst);
			copy_host(src, dst);
		}
		else
		{
			make_path(cmd + 5, src, dst);
			copy(src, dst);
		}
		break;
	case DEL:
		share_mem_write("del\n");
		del_file(cmd + 4);
		break;
	case EXIT:
		share_mem_write("exit\n");
		break;
	case ERR:
		share_mem_write("command not exists\n");
		break;
	default:
		break;
	}
}

void fmt(char* s)
{
	for (int i = 0; i < 20; ++i)
		if (s[i] == ' ')
		{
			s[i] = '\0';
			return;
		}
}

void share_mem()
{
	make_userData();
	unordered_set<string> loginUsers;
	int userCnt = 0;
	mapShare = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, BSIZE, _T("file_system"));
	if (mapShare == INVALID_HANDLE_VALUE)
	{
		printf("share_mem:CreateFileMapping\n");
		return;
	}
	if ((pBegin = (char*)MapViewOfFile(mapShare, FILE_MAP_ALL_ACCESS, 0, 0, BSIZE)) == NULL)
	{
		printf("share_mem:MapViewOfFile\n");
		return;
	}
	string name("PengFeng");
	string fileWrite("fileW");
	fileW = CreateEvent(NULL, TRUE, TRUE, LPCWSTR(fileWrite.c_str()));
	server = CreateEvent(NULL, TRUE, FALSE, _T("serverEvent"));
	user = 0;
	//user = CreateEvent(NULL, TRUE, TRUE, LPCWSTR(name.c_str()));
	write_mem = CreateEvent(NULL, TRUE, TRUE, LPCWSTR("write"));
	while (true)
	{
		WaitForSingleObject(server, INFINITE);
		memcpy(IPC_buf, pBegin, 1024);
		char username[20];
		strncpy(username, IPC_buf, 20);
		fmt(username);
		string Uname(username);
		if (loginUsers.find(Uname) == loginUsers.end())
		{
			userCnt++;
			loginUsers.insert(Uname);
		}
		user = users[Uname].canRead;
		shell_process();
		share_mem_write("command complete\n");
		strcpy_s(pBegin, 1024, IPC_buf);
		ResetEvent(server);
		SetEvent(user);
		SetEvent(fileW);
		if (getCommand(IPC_buf) == EXIT)
		{
			userCnt--;
			loginUsers.erase(Uname);
			if (userCnt == 0)
				break;
		}
	}
	for (auto x : users)
	{
		CloseHandle(x.second.canRead);
		CloseHandle(x.second.hasWriten);
	}
	CloseHandle(fileW);
	CloseHandle(server);
	UnmapViewOfFile(pBegin);
	CloseHandle(mapShare);
}

void fileSys()
{
	fsinit();
	cur_dir = iget(1);
	share_mem();
	iput(cur_dir);
}//run file system

void test()
{
	unordered_map<string, int> userData;
	char name[20];
	strcpy(name, "hello      ");
	userData.insert({ string("hello"),4 });
	cout << "size:" << userData.size() << endl;
	cout << userData["hello"] << endl;
	cout << "size:" << userData.size() << endl;
	cout << userData[name] << endl;
	cout << "size:" << userData.size() << endl;
	cout << userData["hllo"] << endl;
	cout << "size:" << userData.size() << endl;

	strcpy(name, "try");
	string s1("try");
	string s11 = s1 + "tail";
	string s2("try");
	string s22 = s2 + "tail";
	string s3(name);
	HANDLE et = CreateEvent(NULL, TRUE, FALSE, (LPCWSTR)s11.c_str());
	HANDLE st = OpenEvent(EVENT_ALL_ACCESS, FALSE, (LPCWSTR)s22.c_str());
	if (st == INVALID_HANDLE_VALUE || st == 0)
		cout << "nO!" << endl;
	ResetEvent(st);
	WaitForSingleObject(st, 2000);
	cout << "hah" << endl;
	CloseHandle(et);
	CloseHandle(st);
}

int main()
{
	//test();
	disk_init();
	//make_fs();
	fileSys();
	disk_close();
}