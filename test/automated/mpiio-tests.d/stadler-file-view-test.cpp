/*
From: Stadler Hans-Christian <hans-christian.stadler@psi.ch>
To: mpich2-maint@mcs.anl.gov
Subject: [MPICH2 Req #2077] Wrong Transfer Size in MPI::File::Write_at (mpich2-1.0.2p1)
Date: Tue, 8 Nov 2005 14:42:38 +0100

In MPI-2 July 18, 1997

1) on page 225: The offset is always in etype units relative to the current
view.  
2) on page 226: A data access routine attempts to transfer (read or write)
count data items of type datatype.

In the test below, the file consists of 3 consecutive instances of an array of
12 shorts. The fileview of each node consists of subarrays of 4 shorts, one
subarray for each consecutive array instance.  Each of the 3 nodes tries to
write one element ( of datatype short) to the first element (of etype short) of
its own subarray within the second consecutive array instance.

Erroneous behaviour: However, each node writes 4 elements (of datatype short)
to its own subarray.

Thie requirement (2) is violated in this case. The offset is handled correctly
as specified in (1).

Hans-Christian Stadler

*/

#include <iostream>

#undef SEEK_CUR
#undef SEEK_SET
#undef SEEK_END
#include <mpi.h>

using namespace std;

int main (int argc, char *argv[])
{
        MPI::Intracomm &world = MPI::COMM_WORLD;
        MPI::File fh;
        MPI::Datatype &base = MPI::SHORT;
        MPI::Datatype view;
        int size, rank;
        int chunk_size, subchunk_size, subchunk_offset;
        short *buf;
	short *reference;
        int i;

        MPI::Init(argc, argv);
        size = world.Get_size();
        rank = world.Get_rank();
        subchunk_size = 4;
        chunk_size = size * subchunk_size;
        subchunk_offset = subchunk_size * rank;
        buf = new short[chunk_size];
        for (i=0; i<chunk_size; ++i)
                buf[i] = ('0'+rank) << 8 | ('a'+i);

        try {
                view = base.Create_subarray(1, &chunk_size, &subchunk_size, 
				&subchunk_offset, MPI::ORDER_C);
                view.Commit();
                fh = MPI::File::Open(world, argv[1],
				MPI::MODE_RDWR|MPI::MODE_CREATE, 
				MPI::INFO_NULL);
                fh.Write_at(chunk_size*rank*sizeof(short), buf, 
				chunk_size, base);
                fh.Set_view(0, base, view, "native", MPI::INFO_NULL);
                for (i=0; i<chunk_size; ++i)
                        buf[i] = ('0'+rank) << 8 | ('A'+i);
                fh.Write_at(subchunk_size, buf, 1, base);
                fh.Close();
        } catch (MPI::Exception &ex) {
                cerr << "MPI Exception: " << ex.Get_error_string() << endl;
                world.Abort(-1);
        }

        if (! rank) {
		fh = MPI::File::Open(MPI::COMM_SELF, argv[1], 
				MPI::MODE_RDONLY, MPI::INFO_NULL);
		fh.Read(buf, chunk_size*size, MPI::SHORT);
		fh.Close();
		for (int j=0; j<chunk_size*size; j++)
			cout << buf[j] << " ";
		cout << endl;

                cout << "size=" << size << ", chunk_size=" << chunk_size << ", subchunk_size=" << subchunk_size << endl;
                cout << "file_size=" << chunk_size*size*sizeof(short) << endl;
        }
        MPI::Finalize();
        return 0;
}
