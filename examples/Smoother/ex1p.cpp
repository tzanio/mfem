
#include "mfem.hpp"
#include <fstream>
#include <iostream>
#include "Schwarzp.hpp"

using namespace std;
using namespace mfem;

void get_solution(const Vector &x, double &u, double &d2u);
double u_exact(const Vector &x);
double f_exact(const Vector &x);

int isol = 0;
int dim;
double omega;

int main(int argc, char *argv[])
{
   // 1. Initialise MPI
   MPI_Session mpi(argc, argv);

   int num_procs, myid;
   MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
   MPI_Comm_rank(MPI_COMM_WORLD, &myid);
   const char *mesh_file = "../data/one-hex.mesh";
   int order = 1;
   int sdim = 2;
   bool static_cond = false;
   const char *device_config = "cpu";
   bool visualization = true;
   int ref_levels = 1;
   int par_ref_levels = 1;
   int initref = 1;
   // number of wavelengths
   double k = 0.5;
   double theta = 0.5;
   double smth_maxit = 1;
   StopWatch chrono;

   OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh",
                  "Mesh file to use.");
   args.AddOption(&order, "-o", "--order",
                  "Finite element order (polynomial degree) or -1 for"
                  " isoparametric space.");
   args.AddOption(&sdim, "-d", "--dimension", "Dimension");
   args.AddOption(&ref_levels, "-sr", "--serial-refinements",
                  "Number of mesh refinements");
   args.AddOption(&par_ref_levels, "-pr", "--parallel-refinements",
                  "Number of parallel mesh refinements");
   args.AddOption(&initref, "-iref", "--init-refinements",
                  "Number of initial mesh refinements");
   args.AddOption(&k, "-k", "--wavelengths",
                  "Number of wavelengths.");
   args.AddOption(&smth_maxit, "-sm", "--smoother-maxit",
                  "Number of smoothing steps.");
   args.AddOption(&theta, "-th", "--theta",
                  "Dumping parameter for the smoother.");
   args.AddOption(&isol, "-sol", "--solution",
                  "Exact Solution: 0) Polynomial, 1) Sinusoidal.");
   args.AddOption(&static_cond, "-sc", "--static-condensation", "-no-sc",
                  "--no-static-condensation", "Enable static condensation.");
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
   args.Parse();
   if (!args.Good())
   {
      if (mpi.Root())
      {
         args.PrintUsage(cout);
      }
      MPI_Finalize();
      return 1;
   }
   if (mpi.Root())
   {
      args.PrintOptions(cout);
   }
   omega = 2.0 * M_PI * k;

   Mesh *mesh;
   // Define a simple square or cubic mesh
   if (sdim == 2)
   {
      mesh = new Mesh(1, 1, Element::QUADRILATERAL, true, 1.0, 1.0, false);
      // mesh = new Mesh(1, 1, Element::TRIANGLE, true,1.0, 1.0,false);
   }
   else
   {
      mesh = new Mesh(1, 1, 1, Element::HEXAHEDRON, true, 1.0, 1.0, 1.0, false);
   }
   dim = mesh->Dimension();
   for (int i = 0; i < ref_levels; i++)
   {
      mesh->UniformRefinement();
   }

   // 6. Define a parallel mesh 
   ParMesh *pmesh = new ParMesh(MPI_COMM_WORLD, *mesh);
   delete mesh;

   ParMesh cpmesh(*pmesh);
   for (int i = 0; i < par_ref_levels; i++)
   {
      pmesh->UniformRefinement();
   }
   par_patch_nod_info * test = new par_patch_nod_info(&cpmesh,par_ref_levels);



   FiniteElementCollection *fec = new H1_FECollection(order, dim);
   ParFiniteElementSpace *fespace = new ParFiniteElementSpace(pmesh, fec);
   int mycdofoffset = fespace->GetMyDofOffset();

   
   if (myid == 1)
   {
      for (int i = 0; i<pmesh->GetNV(); i++)
      {
         cout << "vertex number, vertex id: " << i << ", " << i+fespace->GetMyDofOffset() << endl; 
         cout << "contributes to: " ; test->vert_contr[i].Print(); 
      }
   }

   if (myid == 1)
   {
      Array<int>edge_vertices;
      for (int i = 0; i<pmesh->GetNEdges(); i++)
      {
         pmesh->GetEdgeVertices(i, edge_vertices);
         cout << "edge vertices are: " << edge_vertices[0]+fespace->GetMyDofOffset() << " and " <<  edge_vertices[1]+fespace->GetMyDofOffset() << endl;
         cout << "edge number: " << i; 
         cout << " contributes to: " ; test->edge_contr[i].Print(); cout << endl;
      }
   }
   int elem_offset;
   int nelem = pmesh->GetNE();
   MPI_Scan(&nelem, &elem_offset, 1, MPI_INT, MPI_SUM,MPI_COMM_WORLD);
   elem_offset -= nelem;
   if (myid == 1)
   {
      for (int i = 0; i<nelem; i++)
      {
         cout << "Element number: " << i+elem_offset;
         cout << " contributes to: " ; test->elem_contr[i].Print(); cout << endl;
      }
   }

   ParGridFunction x(fespace);
   x = 0.0;

   // 16. Send the solution by socket to a GLVis server.
   if (visualization)
   {
      int num_procs, myid;
      MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
      MPI_Comm_rank(MPI_COMM_WORLD, &myid);
      char vishost[] = "localhost";
      int visport = 19916;
      socketstream mesh_sock(vishost, visport);
      mesh_sock << "parallel " << num_procs << " " << myid << "\n";
      mesh_sock.precision(8);

      if (dim == 2)
      {
         mesh_sock << "mesh\n"
                   << *pmesh << "keys nn\n"
                   << flush;
      }
      else
      {
         mesh_sock << "mesh\n"
                   << *pmesh << flush;
      }
   }

   if (visualization)
   {
      int num_procs, myid;
      MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
      MPI_Comm_rank(MPI_COMM_WORLD, &myid);
      char vishost[] = "localhost";
      int visport = 19916;
      socketstream cmesh_sock(vishost, visport);
      cmesh_sock << "parallel " << num_procs << " " << myid << "\n";
      cmesh_sock.precision(8);

      cmesh_sock << "mesh\n" << cpmesh << "keys nn\n"  << flush;
   }

   // 17. Free the used memory.
   delete fespace;
   delete fec;
   delete pmesh;

   return 0;
}

