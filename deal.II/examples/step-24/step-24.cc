/*    $Id: step-4.cc,v 1.34 2006/02/06 21:33:10 wolf Exp $       */
/*    Version: $Name:  $                                          */
/*                                                                */
/*    Copyright (C) 2006 by the deal.II authors */
/*    Author: Xing Jin, Wolfgang Bangerth, Texas A&M University, 2006 */
/*                                                                */
/*    This file is subject to QPL and may not be  distributed     */
/*    without copyright and license information. Please refer     */
/*    to the file deal.II/doc/license.html for the  text  and     */
/*    further information on this license.                        */
                                                          

				 // @sect3{Include files}

				 // The following have all been covered
				 // previously:
#include <base/quadrature_lib.h>
#include <base/function.h>
#include <base/logstream.h>
#include <base/utilities.h>

#include <lac/vector.h>
#include <lac/full_matrix.h>
#include <lac/sparse_matrix.h>
#include <lac/solver_cg.h>
#include <lac/precondition.h>

#include <grid/tria.h>
#include <grid/grid_generator.h>
#include <grid/tria_accessor.h>
#include <grid/tria_iterator.h>
#include <grid/tria_boundary_lib.h>

#include <dofs/dof_handler.h>
#include <dofs/dof_accessor.h>
#include <dofs/dof_tools.h>
#include <dofs/dof_constraints.h>

#include <fe/fe_q.h>
#include <fe/fe_values.h>

#include <numerics/data_out.h>
#include <numerics/matrices.h>
#include <numerics/vectors.h>

#include <fstream>
#include <iostream>
#include <sstream>

				 // This is the only new one: We will need a
				 // library function defined in a class
				 // GridTools that computes the minimal cell
				 // diameter.
#include <grid/grid_tools.h>


				 // @sect3{The "forward problem" class template}

				 // The first part of the main class is
				 // exactly as in @ref step_23 "step-23"
				 // (except for the name):
template <int dim>
class TATForwardProblem
{
  public:
    TATForwardProblem ();
    void run ();
    
  private:
    void setup_system ();
    void solve_p ();
    void solve_v ();
    void output_results () const;

    Triangulation<dim>   triangulation;
    FE_Q<dim>            fe;
    DoFHandler<dim>      dof_handler;

    ConstraintMatrix constraints;
    
    SparsityPattern      sparsity_pattern;
    SparseMatrix<double> system_matrix;
    SparseMatrix<double> mass_matrix;
    SparseMatrix<double> laplace_matrix;

    Vector<double>       solution_p, solution_v;
    Vector<double>       old_solution_p, old_solution_v;
    Vector<double>       system_rhs_p, system_rhs_v;

    double time, time_step;
    unsigned int timestep_number;
    const double theta;

				     //  Here's what's new: first, we need
				     //  that boundary mass matrix $B$ that
				     //  came out of the absorbing boundary
				     //  condition. Likewise, since this time
				     //  we consider a realistic medium, we
				     //  must have a measure of the wave speed
				     //  $c_0$ that will enter all the
				     //  formulas with the Laplace matrix
				     //  (which we still define as $(\nabla
				     //  \phi_i,\nabla \phi_j)$):
    SparseMatrix<double> boundary_matrix;
    const double wave_speed;

				     // The last thing we have to take care of
				     // is that we wanted to evaluate the
				     // solution at a certain number of
				     // detector locations. We need an array
				     // to hold these locations, declared here
				     // and filled in the constructor:
    std::vector<Point<dim> > detector_locations;
};


				 // @sect3{Equation data}

				 // As usual, we have to define our initial
				 // values, boundary conditions, and right
				 // hand side functions. Except things are a
				 // bit simpler this time: we are to consider
				 // a problem that is driven by initial
				 // conditions, so there is no right hand side
				 // function (though you could look up in @ref
				 // step_23 "step-23" to see how this can be
				 // done. Secondly, there are no boundary
				 // conditions: the entire boundary of the
				 // domain consists of absorbing boundary
				 // conditions. That only leaves initial
				 // conditions, and there things are simple
				 // too since the application only needs
				 // initial conditions for the pressure, not
				 // for the velocity.
				 //
				 // So this is all we need: a class that
				 // specifies initial conditions for the
				 // pressure. In the physical setting
				 // considered in this program, these are
				 // small absorbers, which we model as a
				 // series of little circles where we assume
				 // that the pressure surplus is one, whereas
				 // no absorption and therefore no pressure
				 // surplus is anywhere else. This is how we
				 // do things (note that if we wanted to
				 // expand this program to not only compile
				 // but also to run, we would have to
				 // initialize the sources with
				 // three-dimensional source locations):
template <int dim>
class InitialValuesP : public Function<dim> 
{
  public:
    InitialValuesP () : Function<dim>() {};
    
    virtual double value (const Point<dim> &p,
			  const unsigned int  component = 0) const;

  private:
    struct Source
    {
	const Point<dim> location;
	const double     radius;
    };
};


template <int dim>
double InitialValuesP<dim>::value (const Point<dim> &p,
				    const unsigned int /*component*/) const       
{
  static const Source sources[] = {{ Point<dim> (0, 0),         0.025 },
				   { Point<dim> (-0.135, 0),    0.05 },
				   { Point<dim> (0.17, 0),      0.03 },
				   { Point<dim> (-0.25, 0),     0.02 },
				   { Point<dim> (-0.05, -0.15), 0.015 }};
  static const unsigned int n_sources = sizeof(sources)/sizeof(sources[0]);
  
  for (unsigned int i=0; i<n_sources; ++i)
    if (p.distance(sources[i].location) < sources[i].radius)
      return 1;

  return 0;
}


				 // @sect3{Implementation of the <code>TATForwardProblem</code> class}

				 // Let's start again with the
				 // constructor. Setting the member variables
				 // is straightforward. We use the acoustic
				 // wave speed of mineral oil (in millimeters
				 // per microsecond, a common unit in
				 // experimental biomedical imaging) since
				 // this is where many of the experiments we
				 // want to compare the output with are made
				 // in. The Crank-Nicolson scheme is used
				 // again, i.e. theta is set to 0.5. The time
				 // step is later selected to satisfy $k =
				 // \frac h/c$
template <int dim>
TATForwardProblem<dim>::TATForwardProblem ()
		:
                fe (1),
		dof_handler (triangulation),
                theta (0.5),
                wave_speed (1.437)
{
				   // The second task in the constructor is to
				   // initialize the array that holds the
				   // detector locations. The results of this
				   // program were compared with experiments
				   // in which the step size of the detector
				   // spacing is 2.25 degree, corresponding to
				   // 160 detector locations. The radius of
				   // the scanning circle is selected to be
				   // half way between the center and the
				   // boundary to avoid that the remaining
				   // reflections from the imperfect boundary
				   // condition spoils our numerical results.
				   //
				   // The locations of the detectors are then
				   // calculated in clockwise order. Note that
				   // the following of course only works if we
				   // are computing in 2d, a condition that we
				   // guard with an assertion. If we later
				   // wanted to run the same program in 3d, we
				   // would have to add code here for the
				   // initialization of detector locations in
				   // 3d. Due to the assertion, there is no
				   // way we can forget to do this.
  Assert (dim == 2, ExcNotImplemented());
  
  const double detector_step_angle = 2.25;
  const double detector_radius = 0.5;
  
  for (double detector_angle = 2*deal_II_numbers::PI;
       detector_angle >= 0;
       detector_angle -= detector_step_angle/360*2*deal_II_numbers::PI)
    detector_locations.push_back (Point<dim> (std::cos(detector_angle),
					      std::sin(detector_angle)) *
				  detector_radius);
}



				 // @sect4{TATForwardProblem::setup_system}

				 // The following system is pretty much what
				 // we've already done in @ref step_23
				 // "step-23", but with two important
				 // differences. First, we have to create a
				 // circular (or spherical) mesh around the
				 // origin, with a radius of 1. This nothing
				 // new: we've done so before in @ref step_6
				 // "step-6", @ref step_10 "step-10", and @ref
				 // step_11 "step-11", where we also explain
				 // how to attach a boundary object to a
				 // triangulation to be used whenever the
				 // triangulation needs to know where new
				 // boundary points lie when a cell is
				 // refined. Following this, the mesh is
				 // refined a number of times.
				 //
				 // One thing we had to make sure is that the
				 // time step satisfies the CFL condition
				 // discussed in the introduction of @ref
				 // step_23 "step-23". Back in that program,
				 // we ensured this by hand by setting a
				 // timestep that matches the mesh width, but
				 // that was error prone because if we refined
				 // the mesh once more we would also have to
				 // make sure the time step is changed. Here,
				 // we do that automatically: we ask a library
				 // function for the minimal diameter of any
				 // cell. Then we set $k=\frac h{c_0}$. The
				 // only problem is: what exactly is $h$? The
				 // point is that there is really no good
				 // theory on this question for the wave
				 // equation. It is known that for uniformly
				 // refined meshes consisting of rectangles,
				 // $h$ is the minimal edge length. But for
				 // meshes on general quadrilaterals, the
				 // exact relationship appears to be unknown,
				 // i.e. it is unknown what properties of
				 // cells are relevant for the CFL
				 // condition. The problem is that the CFL
				 // condition follows from knowledge of the
				 // smallest eigenvalue of the Laplace matrix,
				 // and that can only be computed analytically
				 // for simply structured meshes.
				 //
				 // The upshot of all this is that we're not
				 // quite sure what exactly we should take for
				 // $h$. The function
				 // GridTools::minimal_cell_diameter computes
				 // the minimal diameter of all cells. If the
				 // cells were all squares or cubes, then the
				 // minimal edge length would be the minimal
				 // diameter divided by
				 // <code>std::sqrt(dim)</code>. We simply
				 // generalize this, without theoretical
				 // justification, to the case of non-uniform
				 // meshes.
				 //
				 // The only other significant change is that
				 // we need to build the boundary mass
				 // matrix. We will comment on this further
				 // down below.
template <int dim>
void TATForwardProblem<dim>::setup_system ()
{
  GridGenerator::hyper_ball (triangulation, Point<dim>(), 1.);
  static const HyperBallBoundary<dim> boundary_description (Point<dim>(), 1.);
  triangulation.set_boundary (0,boundary_description);
  triangulation.refine_global (7);

  time_step = GridTools::minimal_cell_diameter(triangulation) /
	      wave_speed /
	      std::sqrt (1.*dim);
  
  std::cout << "Number of active cells: "
	    << triangulation.n_active_cells()
  	    << std::endl;

  dof_handler.distribute_dofs (fe);

  std::cout << "Number of degrees of freedom: "
	    << dof_handler.n_dofs()
	    << std::endl
	    << std::endl;

  sparsity_pattern.reinit (dof_handler.n_dofs(),
			   dof_handler.n_dofs(),
			   dof_handler.max_couplings_between_dofs());
  DoFTools::make_sparsity_pattern (dof_handler, sparsity_pattern);
  sparsity_pattern.compress();

  system_matrix.reinit (sparsity_pattern);
  mass_matrix.reinit (sparsity_pattern);
  laplace_matrix.reinit (sparsity_pattern);

  MatrixCreator::create_mass_matrix (dof_handler, QGauss<dim>(3),
				     mass_matrix);
  MatrixCreator::create_laplace_matrix (dof_handler, QGauss<dim>(3),
					laplace_matrix);

				   // The second difference, as mentioned, to
				   // @ref step_23 "step-23" is that we need
				   // to build the boundary mass matrix that
				   // grew out of the absorbing boundary
				   // conditions.
				   //
				   // A first observation would be that this
				   // matrix is much sparser than the regular
				   // mass matrix, since none of the shape
				   // functions with purely interior support
				   // contributes to this matrix. We could
				   // therefore optimize the storage pattern
				   // to this situation and build up a second
				   // sparsity pattern that only contains the
				   // nonzero entries that we need. There is a
				   // trade-off to make here: first, we would
				   // have to have a second sparsity pattern
				   // object, so that costs memory. Secondly,
				   // the matrix attached to this sparsity
				   // pattern is going to be smaller and
				   // therefore requires less memore; it would
				   // also be faster to perform matrix-vector
				   // multiplications with it. The final
				   // argument, however, is the one that tips
				   // the scale: we are not primarily
				   // interested in performing matrix-vector
				   // with the boundary matrix alone (though
				   // we need to do that for the right hand
				   // side vector once per time step), but
				   // mostly wish to add it up to the other
				   // matrices used in the first of the two
				   // equations since this is the one that is
				   // going to be multiplied with once per
				   // iteration of the CG method,
				   // i.e. significantly more often. It is now
				   // the case that the SparseMatrix::add
				   // class allows to add one matrix to
				   // another, but only if they use the same
				   // sparsity pattern (the reason being that
				   // we can't add nonzero entries to a matrix
				   // after the sparsity pattern has been
				   // created, so we simply require that the
				   // two matrices have the same sparsity
				   // pattern.
				   //
				   // So let's go with that:
  boundary_matrix.reinit (sparsity_pattern);

				   // The second thing to do is to actually
				   // build the matrix. Here, we need to
				   // integrate over faces of cells, so first
				   // we need a quadrature object that works
				   // on <code>dim-1</code> dimensional
				   // objects. Secondly, the FEFaceValues
				   // variant of FEValues that works on faces,
				   // as its name suggest. And finally, the
				   // other variables that are part of the
				   // assembly machinery. All of this we put
				   // between curly braces to limit the scope
				   // of these variables to where we actually
				   // need them.
				   //
				   // The actual act of assembling the matrix
				   // is then fairly straightforward: we loop
				   // over all cells, over all faces of each
				   // of these cells, and then do something
				   // only if that particular face is at the
				   // boundary of the domain. Like this:
  {
    const QGauss<dim-1>  quadrature_formula(3);
    FEFaceValues<dim> fe_values (fe, quadrature_formula, 
				 update_values  |  update_JxW_values);

    const unsigned int   dofs_per_cell = fe.dofs_per_cell;
    const unsigned int   n_q_points    = quadrature_formula.n_quadrature_points;

    FullMatrix<double>   cell_matrix (dofs_per_cell, dofs_per_cell);

    std::vector<unsigned int> local_dof_indices (dofs_per_cell);

        
                              
    typename DoFHandler<dim>::active_cell_iterator
      cell = dof_handler.begin_active(),
      endc = dof_handler.end();
    for (; cell!=endc; ++cell)
      for (unsigned int f=0; f<GeometryInfo<dim>::faces_per_cell; ++f)
	if (cell->at_boundary(f))
	  {
	    cell_matrix = 0;

	    fe_values.reinit (cell, f);

	    for (unsigned int q_point=0; q_point<n_q_points; ++q_point)
	      for (unsigned int i=0; i<dofs_per_cell; ++i)
		for (unsigned int j=0; j<dofs_per_cell; ++j)
		  cell_matrix(i,j) += (fe_values.shape_value(i,q_point) *
				       fe_values.shape_value(j,q_point) *
				       fe_values.JxW(q_point));

	    cell->get_dof_indices (local_dof_indices);
	    for (unsigned int i=0; i<dofs_per_cell; ++i)
	      for (unsigned int j=0; j<dofs_per_cell; ++j)
		boundary_matrix.add (local_dof_indices[i],
				     local_dof_indices[j],
				     cell_matrix(i,j));
	  }
  
  }

  system_matrix.copy_from (mass_matrix);
  system_matrix.add (time_step * time_step * theta * theta *
		     wave_speed * wave_speed,
		     laplace_matrix);
  system_matrix.add (wave_speed * theta * time_step, boundary_matrix);
  

  solution_p.reinit (dof_handler.n_dofs());
  old_solution_p.reinit (dof_handler.n_dofs());
  system_rhs_p.reinit (dof_handler.n_dofs());

  solution_v.reinit (dof_handler.n_dofs());
  old_solution_v.reinit (dof_handler.n_dofs());
  system_rhs_v.reinit (dof_handler.n_dofs());

  constraints.close ();
}


				 // @sect4{TATForwardProblem::solve_p and TATForwardProblem::solve_v}

				 // The following two functions, solving the
				 // linear systems for the pressure and the
				 // velocity variable, are taken pretty much
				 // verbatim (with the exception of the change
				 // of name from $u$ to $p$ of the primary
				 // variable) from @ref step_23 "step-23":
template <int dim>
void TATForwardProblem<dim>::solve_p () 
{
  SolverControl           solver_control (1000, 1e-8*system_rhs_p.l2_norm());
  SolverCG<>              cg (solver_control);

  cg.solve (system_matrix, solution_p, system_rhs_p,
	    PreconditionIdentity());

  std::cout << "   p-equation: " << solver_control.last_step()
	    << " CG iterations."
	    << std::endl;
}


template <int dim>
void TATForwardProblem<dim>::solve_v () 
{
  SolverControl           solver_control (1000, 1e-8*system_rhs_v.l2_norm());
  SolverCG<>              cg (solver_control);

  cg.solve (mass_matrix, solution_v, system_rhs_v,
	    PreconditionIdentity());

  std::cout << "   v-equation: " << solver_control.last_step()
	    << " CG iterations."
	    << std::endl;
}



				 // @sect4{TATForwardProblem::output_results}

				 // The same holds here: the function is from
				 // @ref step_23 "step-23".
template <int dim>
void TATForwardProblem<dim>::output_results () const
{
  DataOut<dim> data_out;

  data_out.attach_dof_handler (dof_handler);
  data_out.add_data_vector (solution_p, "P");
  data_out.add_data_vector (solution_v, "V");

  data_out.build_patches ();

  std::ostringstream filename;
  filename << "solution-"
	   << Utilities::int_to_string (timestep_number, 3)
	   << ".gnuplot";
  std::ofstream output (filename.str().c_str());
  data_out.write_gnuplot (output);
}


//XXX
                                 // This is the main function
                                 // 
                                      
template <int dim>
void TATForwardProblem<dim>::run () 
{
  setup_system();

  VectorTools::project (dof_handler,constraints, 
			QGauss<dim>(3), InitialValuesP<dim>(),
			old_solution_p);

  old_solution_v = 0;


  timestep_number = 1;
  unsigned int n_steps;
  unsigned int n_detectors;

				   // Number of time steps is defined as the
				   // ratio of the total time to the time step
  
  const double end_time = 0.7;
  n_steps=static_cast<unsigned int>(std::floor(end_time/time_step));     
  

				   // Define a vector to hold the value obtained
				   // by the detector
  Vector<double> project_dat (n_steps * n_detectors +1);

  std::cout<< "Total number of time steps = "<< n_steps <<std::endl;
  std::cout<< "Total number of detectors = "<< n_detectors << std::endl;

				   // Open a file to write the data
				   // obtained by the detectors 
                         
  std::ofstream proj_out;
  proj_out.open("proj.dat");
  

  Vector<double> tmp1 (solution_p.size());
  Vector<double> tmp2 (solution_v.size());
  Vector<double> G1 (solution_p.size());
  Vector<double> G2 (solution_v.size());
  
  for (double time = time_step; time<=end_time; time+=time_step, ++timestep_number)
    {
      std::cout << std::endl;                                       
      std::cout<< "time_step " << timestep_number << " @ t=" << time << std::endl;

				       // Calculate G1 as defined in the
				       // introduction section
      mass_matrix.vmult (tmp1, old_solution_p); 
      mass_matrix.vmult (tmp2, old_solution_v); 
      G1 = tmp1;
      G1.add(time_step * (1-theta), tmp2);
      
				       // Calculate G2 as defined in the
				       // introduction section
      mass_matrix.vmult (tmp1, old_solution_v);
      laplace_matrix.vmult (tmp2, old_solution_p); 
      G2 = tmp1;
      G2.add(-wave_speed*wave_speed*time_step*(1-theta), tmp2);
      tmp1=0;
      boundary_matrix.vmult (tmp1,old_solution_p);
      G2.add(wave_speed,tmp1);
      
				       // Compute the pressure potential p, the formula
				       // has been presented in the introduction section

      system_rhs_p = G1; 
      system_rhs_p.add(time_step * theta , G2);

      solve_p ();

				       // Compute the derivative potential pressure.
				       // The formula has been presented in the introduction
				       // section. The potential derivative is calculated 
				       // after the potential pressure because the calculation
				       // depends on the current value of the potential 
				       // pressure

      system_rhs_v = G2;
      tmp1 = 0;
      laplace_matrix.vmult (tmp1, solution_p);
      system_rhs_v.add(-time_step * theta*wave_speed*wave_speed, tmp1);
      tmp1 = 0;
      boundary_matrix.vmult(tmp1, solution_p);
      system_rhs_v.add(-wave_speed,tmp1);
      
      solve_v ();
				       // Compute the energy in the system.By checking
				       // energy change in the system, we can verify
				       // the correctness of the code. 

      double energy = (mass_matrix.matrix_scalar_product(solution_v,solution_v)+
		       wave_speed * wave_speed * laplace_matrix.matrix_scalar_product(solution_p,solution_p))/2;        
                                                                         
      std::cout << "energy= " << energy << std::endl;

      output_results ();
      
				       //  Evaluate the value at specific locations. 
				       //  For 2-D, it is on a circle. For 1-D, 
				       //  it is a point detector.

      proj_out << time ;

      for (unsigned i=0 ; i<=detector_locations.size(); ++i)
	{
	  project_dat((timestep_number-1)*n_detectors+i)
	    = VectorTools::point_value (dof_handler,
					solution_p, 
					detector_locations[i]);
	  proj_out << " "<< project_dat((timestep_number-1)*n_detectors+i)<<" " ;
	}

      proj_out<<std::endl;
          
				       // Update the values for the pressure potential 
				       // and its derivative. 
         
      old_solution_p = solution_p;
      solution_p = 0;
      old_solution_v = solution_v;      
      solution_v = 0;
    }
}



				 // @sect3{The <code>main</code> function}

				 // What remains is the main function of the
				 // program. There is nothing here that hasn't
				 // been shown in several of the previous
				 // programs:
int main () 
{
  try
    {
      deallog.depth_console (0);
      TATForwardProblem<2> forward_problem_solver;
      forward_problem_solver.run ();
    }
  catch (std::exception &exc)
    {
      std::cerr << std::endl << std::endl
		<< "----------------------------------------------------"
		<< std::endl;
      std::cerr << "Exception on processing: " << std::endl
		<< exc.what() << std::endl
		<< "Aborting!" << std::endl
		<< "----------------------------------------------------"
		<< std::endl;

      return 1;
    }
  catch (...) 
    {
      std::cerr << std::endl << std::endl
		<< "----------------------------------------------------"
		<< std::endl;
      std::cerr << "Unknown exception!" << std::endl
		<< "Aborting!" << std::endl
		<< "----------------------------------------------------"
		<< std::endl;
      return 1;
    }
  
  return 0;
}
