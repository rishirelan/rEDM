#include "block_lnlp.h"

/*** Constructors ***/
BlockLNLP::BlockLNLP(): 
    block(std::vector<vec>()), tp(0), E(0), embedding(std::vector<size_t>()), target(0), 
    remake_vectors(true), remake_targets(true), remake_ranges(true)
{
}

void BlockLNLP::set_time(const NumericVector new_time)
{
    time = as<std::vector<double> >(new_time);
    return;
}

void BlockLNLP::set_block(const NumericMatrix new_block)
{
    size_t num_cols = size_t(new_block.ncol());
    block.resize(num_cols);
    num_vectors = size_t(new_block.nrow());
    for(size_t i = 0; i < num_cols; ++i)
    {
        block[i].resize(num_vectors);
        for(size_t j = 0; j < num_vectors; ++j)
            block[i][j] = new_block(j,i);
    }
    init_distances();
    return;
}

void BlockLNLP::set_norm(const double norm)
{
    if(norm == 1)
    {
        norm_mode = L1_NORM;
    } else if (norm == 2) {
        norm_mode = L2_NORM;
    } else {
        norm_mode = P_NORM;
        p = norm;
    }
    return;
}

void BlockLNLP::set_pred_type(const int pred_type)
{
    switch(pred_type)
    {
        case 1:
            pred_mode = SMAP;
            break;
        case 2:
            pred_mode = SIMPLEX;
            break;
        case 3:
            pred_mode = FAST_LINEAR;
            break;
        default:
            throw(std::domain_error("unknown pred type selected"));
    }
    return;
}

void BlockLNLP::set_lib(const NumericMatrix lib)
{
    size_t num_rows = size_t(lib.nrow());
    lib_ranges.resize(num_rows);
    for(size_t i = 0; i < num_rows; ++i)
    {
        lib_ranges[i].first = lib(i,0) - 1; // convert 1-index to 0-index
        lib_ranges[i].second = lib(i,1) - 1;
    }
    remake_ranges = true;
    return;
}

void BlockLNLP::set_pred(const NumericMatrix pred)
{
    size_t num_rows = size_t(pred.nrow());
    pred_ranges.resize(num_rows);
    for(size_t i = 0; i < num_rows; ++i)
    {
        pred_ranges[i].first = pred(i,0) - 1; // convert 1-index to 0-index
        pred_ranges[i].second = pred(i,1) - 1;
    }
    remake_ranges = true;
    return;
}

void BlockLNLP::set_exclusion_radius(const double new_exclusion_radius)
{
    exclusion_radius = new_exclusion_radius;
    if(exclusion_radius >= 0)
        CROSS_VALIDATION = true;
    return;
}

void BlockLNLP::set_epsilon(const double new_epsilon)
{
    epsilon = new_epsilon;
    return;
}

void BlockLNLP::set_embedding(const NumericVector new_embedding)
{
    embedding = as<std::vector<size_t> >(new_embedding);
    E = embedding.size();
    remake_vectors = true;
    return;
}

void BlockLNLP::set_target_column(const size_t new_target)
{
    target = new_target;
    remake_targets = true;
    return;
}

void BlockLNLP::set_params(const int new_tp, const size_t new_nn)
{
    if(tp != new_tp)
        remake_targets = true;
    if(remake_vectors || remake_targets)
        remake_ranges = true;
    
    tp = new_tp;
    nn = new_nn;    
    return;
}

void BlockLNLP::set_theta(const double new_theta)
{
    theta = new_theta;
    return;
}

void BlockLNLP::suppress_warnings()
{
    SUPPRESS_WARNINGS = true;
    return;
}

void BlockLNLP::save_smap_coefficients()
{
    SAVE_SMAP_COEFFICIENTS = true;
    return;
}

void BlockLNLP::run()
{
    prepare_forecast(); // check parameters
    forecast(); // forecast code is in forecast_machine
    return;
}

DataFrame BlockLNLP::get_output()
{
    std::vector<size_t> pred_idx = which_indices_true(pred_requested_indices);
    vec short_time(pred_idx.size(), qnan);
    vec short_obs(pred_idx.size(), qnan);
    vec short_pred(pred_idx.size(), qnan);
    vec short_pred_var(pred_idx.size(), qnan);
    
    for(size_t i = 0; i < pred_idx.size(); ++i)
    {
        short_time[i] = target_time[pred_idx[i]];
        short_obs[i] = targets[pred_idx[i]];
        short_pred[i] = predicted[pred_idx[i]];
        short_pred_var[i] = predicted_var[pred_idx[i]];
    }
    
    return DataFrame::create( Named("time") = short_time, 
                              Named("obs") = short_obs, 
                              Named("pred") = short_pred, 
                              Named("pred_var") = short_pred_var);
}

DataFrame BlockLNLP::get_smap_coefficients()
{
    std::vector<size_t> pred_idx = which_indices_true(pred_requested_indices);
    size_t embed_dim = smap_coefficients.size();
    List tmp_lst(embed_dim);
    CharacterVector df_names(embed_dim);
    vec temp_coeff;
    for(size_t j = 0; j < embed_dim; ++j)
    {
        temp_coeff.assign(pred_idx.size(), qnan);
        for(size_t i = 0; i < pred_idx.size(); ++i)
        {
            temp_coeff[i] = smap_coefficients[j][pred_idx[i]];
        }
        tmp_lst[j] = temp_coeff;
        df_names[j] = "c_" + std::to_string(j+1);
    }
    df_names[embed_dim - 1] = "c_0";
    DataFrame df(tmp_lst);
    df.attr("names") = df_names;
    return(df);
}

List BlockLNLP::get_smap_coefficient_covariances()
{
    std::vector<size_t> pred_idx = which_indices_true(pred_requested_indices);
    List tmp_lst(pred_idx.size());
    for(size_t i = 0; i < pred_idx.size(); ++i)
    {
        tmp_lst[i] = smap_coefficient_covariances[pred_idx[i]];
    }
    return(tmp_lst);
}

DataFrame BlockLNLP::get_stats()
{
    PredStats output = make_stats();
    PredStats const_output = make_const_stats();
    return DataFrame::create( Named("num_pred") = output.num_pred, 
                              Named("rho") = output.rho, 
                              Named("mae") = output.mae, 
                              Named("rmse") = output.rmse,
                              Named("perc") = output.perc, 
                              Named("p_val") = output.p_val, 
                              Named("const_pred_num_pred") = const_output.num_pred, 
                              Named("const_pred_rho") = const_output.rho, 
                              Named("const_pred_mae") = const_output.mae, 
                              Named("const_pred_rmse") = const_output.rmse, 
                              Named("const_pred_perc") = const_output.perc, 
                              Named("const_p_val") = const_output.p_val);
}

// *** PRIVATE METHODS FOR INTERNAL USE ONLY *** //

void BlockLNLP::prepare_forecast()
{
    if(remake_vectors)
    {
        make_vectors();
        init_distances();
    }
    
    if(remake_targets)
        make_targets();
        
    if(remake_ranges)
    {
        set_indices_from_range(lib_indices, lib_ranges, 0, -std::max(0, tp), true);
        set_indices_from_range(pred_indices, pred_ranges, 0, -std::max(0, tp), false);
        set_pred_requested_indices_from_range(pred_requested_indices, pred_ranges);

        check_cross_validation();

        which_lib = which_indices_true(lib_indices);
        which_pred = which_indices_true(pred_indices);

        remake_ranges = false;
    }
    
    compute_distances();
    //sort_neighbors();
    
    return;
}

void BlockLNLP::make_vectors()
{
    data_vectors.assign(num_vectors, vec(E, qnan));
    for(size_t i = 0; i < num_vectors; ++i)
    {
        for(size_t j = 0; j < E; ++j)
        {
            data_vectors[i][j] = block[embedding[j]-1][i];
        }
    }

    remake_vectors = false;
    return;
}

void BlockLNLP::make_targets()
{
    if((target < 1) || (target-1 >= block.size()))
    {
        throw std::domain_error("invalid target column");
    }
    
    targets.clear();
    if(tp >= 0)
    {
        targets.assign(block[target-1].begin()+tp, block[target-1].end());
        targets.insert(targets.end(), tp, qnan);
        target_time.assign(time.begin()+tp, time.end());
        target_time.insert(target_time.end(), tp, qnan);
    }
    else
    {
        targets.assign(block[target-1].begin(), block[target-1].end()+tp);
        targets.insert(targets.begin(), -tp, qnan);
        target_time.assign(time.begin(), time.end()+tp);
        target_time.insert(target_time.begin(), -tp, qnan);
    }
    const_targets = block[target-1];
    remake_targets = false;
    return;
}


RCPP_MODULE(block_lnlp_module)
{
    class_<BlockLNLP>("BlockLNLP")
    
    .constructor()

    .method("set_time", &BlockLNLP::set_time)
    .method("set_block", &BlockLNLP::set_block)
    .method("set_norm", &BlockLNLP::set_norm)
    .method("set_pred_type", &BlockLNLP::set_pred_type)
    .method("set_lib", &BlockLNLP::set_lib)
    .method("set_pred", &BlockLNLP::set_pred)
    .method("set_exclusion_radius", &BlockLNLP::set_exclusion_radius)
    .method("set_epsilon", &BlockLNLP::set_epsilon)
    .method("set_embedding", &BlockLNLP::set_embedding)
    .method("set_target_column", &BlockLNLP::set_target_column)
    .method("set_params", &BlockLNLP::set_params)
    .method("set_theta", &BlockLNLP::set_theta)
    .method("suppress_warnings", &BlockLNLP::suppress_warnings)
    .method("save_smap_coefficients", &BlockLNLP::save_smap_coefficients)
    .method("run", &BlockLNLP::run)
    .method("get_output", &BlockLNLP::get_output)
    .method("get_smap_coefficients", &BlockLNLP::get_smap_coefficients)
    .method("get_smap_coefficient_covariances", &BlockLNLP::get_smap_coefficient_covariances)
    .method("get_stats", &BlockLNLP::get_stats)
    ;
}
